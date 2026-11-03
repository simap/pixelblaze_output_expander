#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "loopback.h"
#include "app.h"
#include "outputexpander.pio.h"

volatile loopback_t loopback;

static PIO pio;
static uint sm;
static uint uartOffset, spiOffset;

static bool active;
static bool spiMode;
static uint32_t idx;
static uint32_t len;
static uint32_t passes;
static uint32_t pausedUntil;
static bool tailSent;

void loopback_init(void) {
    pio = pio2;
    sm = PBX_LOOPBACK_SM;
    uartOffset = pio_add_program(pio, &uart_tx_program);
    spiOffset = pio_add_program(pio, &spi_tx_program);
}

static void start_uart(void) {
    uint pin = PBX_DATA_IN_SPI_PIN;
    if (loopback.rxBaud)
        stats.baud = uart_set_baudrate(uart0, loopback.rxBaud);
    uint32_t baud = loopback.baud ? loopback.baud : stats.baud;
    pio_sm_config c = uart_tx_program_get_default_config(uartOffset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float) clock_get_hz(clk_sys) / (8.0f * baud));
    pio_sm_set_pins_with_mask(pio, sm, 1u << pin, 1u << pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_gpio_init(pio, pin);
    pio_sm_init(pio, sm, uartOffset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static void start_spi(void) {
    uint data = PBX_DATA_IN_SPI_PIN;
    uint clk = PBX_CLK_IN_PIN;
    uint32_t hz = loopback.spiHz ? loopback.spiHz : 1000000;
    pio_sm_config c = spi_tx_program_get_default_config(spiOffset);
    sm_config_set_out_pins(&c, data, 1);
    sm_config_set_sideset_pins(&c, clk);
    sm_config_set_out_shift(&c, false, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float) clock_get_hz(clk_sys) / (4.0f * hz));
    pio_sm_set_pins_with_mask(pio, sm, 0, (1u << data) | (1u << clk));
    pio_sm_set_consecutive_pindirs(pio, sm, data, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, clk, 1, true);
    pio_gpio_init(pio, data);
    pio_gpio_init(pio, clk);
    pio_sm_init(pio, sm, spiOffset, &c);

    // Total bit count into x, then drain the OSR so the first out autopulls.
    uint32_t bits = len * 8 * passes + (loopback.extraBits & 7);
    pio_sm_put(pio, sm, bits - 1);
    pio_sm_exec(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec(pio, sm, pio_encode_out(pio_null, 32));
    pio_sm_set_enabled(pio, sm, true);
}

static void start(void) {
    len = loopback.len;
    if (len > LOOPBACK_MAX)
        len = LOOPBACK_MAX;
    passes = loopback.repeat ? loopback.repeat : 1;
    idx = 0;
    pausedUntil = 0;
    tailSent = false;
    spiMode = loopback.mode == 1;
    if (spiMode)
        start_spi();
    else
        start_uart();
    active = true;
}

static void stop(void) {
    pio_sm_set_enabled(pio, sm, false);
    // Hand the pads back to the receivers as inputs.
    pio_sm_set_consecutive_pindirs(pio, sm, PBX_DATA_IN_SPI_PIN, 1, false);
    if (spiMode)
        pio_sm_set_consecutive_pindirs(pio, sm, PBX_CLK_IN_PIN, 1, false);
    active = false;
    loopback.trigger = 0;
}

static inline void put_byte(uint8_t b) {
    pio_sm_put(pio, sm, spiMode ? (uint32_t) b << 24 : b);
}

void loopback_poll(void) {
    if (!active) {
        if (loopback.trigger)
            start();
        return;
    }

    if (pausedUntil && (int32_t) (time_us_32() - pausedUntil) < 0)
        return;
    pausedUntil = 0;

    uint32_t chunk = loopback.chunk;
    while (idx < len && !pio_sm_is_tx_fifo_full(pio, sm)) {
        put_byte(loopback.data[idx++]);
        if (chunk && idx % chunk == 0 && idx < len) {
            pausedUntil = time_us_32() + loopback.gapUs;
            if (!pausedUntil)
                pausedUntil = 1;
            return;
        }
    }

    if (idx < len)
        return;
    if (!pio_sm_is_tx_fifo_empty(pio, sm))
        return;

    if (passes) {
        loopback.sent++;
        if (--passes) {
            idx = 0;
            return;
        }
    }

    if (spiMode) {
        // Feed the partial byte, then wait for the program to park.
        if ((loopback.extraBits & 7) && !tailSent) {
            put_byte(0xff);
            tailSent = true;
            return;
        }
        if (pio_sm_get_pc(pio, sm) != spiOffset + spi_tx_offset_done)
            return;
    } else {
        // The last byte is still shifting when the FIFO empties. Two byte
        // times covers it and the stop bit.
        uint32_t baud = loopback.baud ? loopback.baud : stats.baud;
        sleep_us(20000000u / baud + 1);
    }
    stop();
}
