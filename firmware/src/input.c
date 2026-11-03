// Receive path. UART0 and SPI0 each DMA into an 8 KB ring in endless mode.
// Both run all the time. The SPI ring is served once it shows PBX_SPI_LIVE_BYTES
// of activity and keeps priority until it has been quiet for PBX_SPI_HOLD_MS.
// While SPI is live the UART ring is discarded, since the UART receiver sees
// the shared data line toggling at SPI rates.
//
// Frames are copied out of the ring by a memory to memory DMA with the
// sniffer computing CRC32, so the CPU never runs a CRC loop.

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/uart.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/structs/uart.h"
#include "outputexpander.pio.h"
#include "input.h"
#include "parser.h"
#include "crc32.h"
#include "app.h"

#define RING_BITS 13
#define RING_SIZE (1u << RING_BITS)

static uint8_t uartRingBuf[RING_SIZE] __attribute__((aligned(RING_SIZE)));
static uint8_t spiRingBuf[RING_SIZE] __attribute__((aligned(RING_SIZE)));

static ring_t uartRing = { uartRingBuf, RING_SIZE - 1, RING_BITS, 0, 0 };
static ring_t spiRing = { spiRingBuf, RING_SIZE - 1, RING_BITS, 0, 0 };
static parser_t uartParser, spiParser;

static int uartDma, spiDma, crcDma;

static enum pbx_input input = PBX_INPUT_UART;
static uint32_t uartSeen, spiSeen;
static uint32_t spiBurst;
static uint32_t lastSpiMs;

static void uart_irq(void) {
    uart_hw_t *hw = uart_get_hw(uart0);
    uint32_t ris = hw->ris;
    hw->icr = ris;
    if (ris & (UART_UARTRIS_OERIS_BITS | UART_UARTRIS_FERIS_BITS | UART_UARTRIS_PERIS_BITS | UART_UARTRIS_BERIS_BITS))
        stats.uartErrors++;
}

static void start_rx_dma(int dma, ring_t *ring, const volatile void *src, uint dreq) {
    dma_channel_config c = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);
    channel_config_set_dreq(&c, dreq);
    dma_channel_configure(dma, &c, ring->buf, src, dma_encode_endless_transfer_count(), true);
}

static void uart_input_init(void) {
    uart_init(uart0, PBX_UART_BAUD);
    stats.baud = uart_set_baudrate(uart0, PBX_UART_BAUD);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    gpio_set_function(PBX_DATA_IN_UART_PIN, UART_FUNCSEL_NUM(uart0, PBX_DATA_IN_UART_PIN));
    gpio_pull_up(PBX_DATA_IN_UART_PIN);  // idle high with no sender attached

    uart_hw_t *hw = uart_get_hw(uart0);
    hw->dmacr = UART_UARTDMACR_RXDMAE_BITS;
    hw->imsc = UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_FEIM_BITS | UART_UARTIMSC_PEIM_BITS | UART_UARTIMSC_BEIM_BITS;
    irq_set_exclusive_handler(UART0_IRQ, uart_irq);
    irq_set_enabled(UART0_IRQ, true);

    uartDma = dma_claim_unused_channel(true);
    start_rx_dma(uartDma, &uartRing, &hw->dr, uart_get_dreq(uart0, false));
}

// PIO receiver on PIO2. IN base is the data pin and the clock sits 2 above
// it, which the program relies on.
static void spi_input_init(void) {
    PIO pio = pio2;
    uint sm = PBX_SPI_RX_SM;
#if PBX_SPI_CPHA
    const pio_program_t *prog = &spi_rx_cpha1_program;
    uint offset = pio_add_program(pio, prog);
    pio_sm_config c = spi_rx_cpha1_program_get_default_config(offset);
#else
    const pio_program_t *prog = &spi_rx_program;
    uint offset = pio_add_program(pio, prog);
    pio_sm_config c = spi_rx_program_get_default_config(offset);
#endif
    (void) prog;
    sm_config_set_in_pins(&c, PBX_DATA_IN_SPI_PIN);
    sm_config_set_jmp_pin(&c, PBX_CLK_IN_PIN);
    sm_config_set_in_shift(&c, false, true, 8);
    pio_gpio_init(pio, PBX_DATA_IN_SPI_PIN);
    pio_gpio_init(pio, PBX_CLK_IN_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, PBX_DATA_IN_SPI_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm, PBX_CLK_IN_PIN, 1, false);
    gpio_pull_down(PBX_CLK_IN_PIN);
    pio_sm_init(pio, sm, offset, &c);

    // Idle timeout in 2 cycle loop iterations, parked in the OSR. Loaded
    // through the TX FIFO before the FIFOs are joined on the RX side.
    pio_sm_put(pio, sm, (clock_get_hz(clk_sys) / 1000000) * PBX_SPI_IDLE_US / 2);
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, true));
    hw_set_bits(&pio->sm[sm].shiftctrl, PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS);

    spiDma = dma_claim_unused_channel(true);
    start_rx_dma(spiDma, &spiRing, &pio->rxf[sm], pio_get_dreq(pio, sm, false));
    pio_sm_set_enabled(pio, sm, true);
}

static void crc_init(void) {
    crcDma = dma_claim_unused_channel(true);
    dma_sniffer_enable(crcDma, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
    dma_sniffer_set_output_reverse_enabled(true);
    dma_sniffer_set_output_invert_enabled(true);

    // Self test against the standard CRC32 check value.
    static const uint8_t check[9] = "123456789";
    static uint8_t scratch[16] __attribute__((aligned(16)));
    ring_t t = { scratch, 15, 4, 0, 9 };
    memcpy(scratch, check, 9);
    uint8_t out[9];
    // Two chunks, so the accumulator carrying across transfers is covered.
    port_copy_crc(&t, 0, 4, out, true);
    stats.crcSelfTest = port_copy_crc(&t, 4, 5, out + 4, false);
    stats.crcSelfTestSw = crc32_buf(check, 9);
}

uint32_t port_copy_crc(const ring_t *ring, uint32_t start, uint32_t len, uint8_t *dst, bool first) {
    dma_channel_config c = dma_channel_get_default_config(crcDma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, false, ring->sizeBits);
    channel_config_set_sniff_enable(&c, true);
    if (first)
        dma_sniffer_set_data_accumulator(0xffffffffu);
    dma_channel_configure(crcDma, &c, dst, ring->buf + (start & ring->mask), len, true);
    dma_channel_wait_for_finish_blocking(crcDma);
    return dma_sniffer_get_data_accumulator();
}

void input_init(void) {
    crc_init();
    uart_input_init();
    spi_input_init();
    parser_init(&uartParser, &uartRing);
    parser_init(&spiParser, &spiRing);
}

static inline uint32_t dma_write_index(int dma, const ring_t *ring) {
    return (dma_hw->ch[dma].write_addr - (uintptr_t) ring->buf) & ring->mask;
}

void input_poll(void) {
    uint32_t now = port_millis();

    uartRing.wr = dma_write_index(uartDma, &uartRing);
    spiRing.wr = dma_write_index(spiDma, &spiRing);

    uint32_t uartNew = (uartRing.wr - uartSeen) & uartRing.mask;
    uint32_t spiNew = (spiRing.wr - spiSeen) & spiRing.mask;
    uartSeen = uartRing.wr;
    spiSeen = spiRing.wr;
    stats.uartBytes += uartNew;
    stats.spiBytes += spiNew;
    if (spiNew) {
        spiBurst += spiNew;
        lastSpiMs = now;
    }

    if (input != PBX_INPUT_SPI && spiBurst >= PBX_SPI_LIVE_BYTES) {
        input = PBX_INPUT_SPI;
    }
    if (now - lastSpiMs > PBX_SPI_HOLD_MS) {
        // Quiet SPI. Drop anything below the live threshold and, if SPI was
        // live, hand the parser back to the UART ring with a clean slate.
        spiBurst = 0;
        spiRing.rd = spiRing.wr;
        parser_reset(&spiParser);
        if (input == PBX_INPUT_SPI) {
            input = PBX_INPUT_UART;
            uartRing.rd = uartRing.wr;
            parser_reset(&uartParser);
        }
    }
    stats.input = input;

    if (input == PBX_INPUT_SPI) {
        uartRing.rd = uartRing.wr;
        parser_reset(&uartParser);
        parser_poll(&spiParser);
    } else {
        parser_poll(&uartParser);
    }
}
