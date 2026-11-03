// Output. Channels 0 to 3 run on PIO0 state machines 0 to 3 and channels 4
// to 7 on PIO1, each with its own DMA channel feeding the TX FIFO from the
// channel's draw buffer. A draw configures every active state machine for
// its channel's LED type, starts the DMA, then enables all of them together.
//
// APA102 clock and data channels share one clock divider and the two
// programs are two cycles per bit, so a clock channel started with its data
// channels stays in lockstep. Both PIO blocks are enabled by one CTRL write
// so all eight state machines start on the same system clock.

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "output.h"
#include "channels.h"
#include "app.h"
#include "outputexpander.pio.h"

static PIO pios[2];
static uint wsOffset[2], apaDataOffset[2], apaClkOffset[2];
static int outDma[PBX_CHANNELS];

static bool busy;
static uint32_t doneUs;
static uint8_t activeMask;   // channels with a DMA transfer in this draw
static uint8_t clockMask;    // APA102 clock channels in this draw

static inline PIO ch_pio(int ch) { return pios[ch / 4]; }
static inline uint ch_sm(int ch) { return ch % 4; }
static inline uint ch_pin(int ch) { return PBX_LED_OUT_PIN(ch); }

void output_init(void) {
    pios[0] = pio0;
    pios[1] = pio1;
    for (int b = 0; b < 2; b++) {
        wsOffset[b] = pio_add_program(pios[b], &ws2812_program);
        apaDataOffset[b] = pio_add_program(pios[b], &apa102_data_program);
        apaClkOffset[b] = pio_add_program(pios[b], &apa102_clk_program);
    }
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        PIO pio = ch_pio(ch);
        uint sm = ch_sm(ch);
        uint pin = ch_pin(ch);
        pio_gpio_init(pio, pin);
        pio_sm_set_pins_with_mask(pio, sm, 0, 1u << pin);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
        outDma[ch] = dma_claim_unused_channel(true);
    }
}

bool output_busy(void) {
    if (!busy)
        return false;
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        if (!(activeMask & (1u << ch)))
            continue;
        if (dma_channel_is_busy(outDma[ch]))
            return true;
        // TXSTALL sets once the state machine has run out of data, so the
        // FIFO and shift register are drained.
        if (!(ch_pio(ch)->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + ch_sm(ch)))))
            return true;
    }
    busy = false;
    doneUs = time_us_32();
    return false;
}

static void start_dma(int ch, const ch_buffer_t *buf) {
    PIO pio = ch_pio(ch);
    uint sm = ch_sm(ch);
    dma_channel_config c = dma_channel_get_default_config(outDma[ch]);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(outDma[ch], &c, &pio->txf[sm], buf->data, buf->cfg.words, true);
}

static void setup_ws2812(int ch, const ch_buffer_t *buf) {
    PIO pio = ch_pio(ch);
    uint sm = ch_sm(ch);
    pio_sm_config c = ws2812_program_get_default_config(wsOffset[ch / 4]);
    sm_config_set_sideset_pins(&c, ch_pin(ch));
    sm_config_set_out_shift(&c, false, true, buf->cfg.bitsPerWord);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    float div = (float) clock_get_hz(clk_sys) / (PBX_WS2812_BIT_HZ * (ws2812_T1 + ws2812_T2 + ws2812_T3));
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, wsOffset[ch / 4], &c);
    start_dma(ch, buf);
}

static void setup_apa102_data(int ch, const ch_buffer_t *buf, float div) {
    PIO pio = ch_pio(ch);
    uint sm = ch_sm(ch);
    pio_sm_config c = apa102_data_program_get_default_config(apaDataOffset[ch / 4]);
    sm_config_set_out_pins(&c, ch_pin(ch), 1);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, apaDataOffset[ch / 4], &c);
    start_dma(ch, buf);
    // Fill the shift register before enable so the first bit goes out on
    // the first cycle, in step with the clock program.
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, true));
}

static void setup_apa102_clk(int ch, uint32_t bits, float div) {
    PIO pio = ch_pio(ch);
    uint sm = ch_sm(ch);
    pio_sm_config c = apa102_clk_program_get_default_config(apaClkOffset[ch / 4]);
    sm_config_set_sideset_pins(&c, ch_pin(ch));
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, apaClkOffset[ch / 4], &c);
    pio_sm_put(pio, sm, bits - 1);
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_x, pio_osr));
}

void output_draw(void) {
    if (output_busy()) {
        stats.overDraw++;
        return;
    }

    // Survey candidates and current buffers before committing, so a draw
    // that cannot start leaves the candidates for the next one.
    bool anyWs = false;
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        const ch_buffer_t *b = channels[ch].candidate ? channels[ch].candidate : channels[ch].draw;
        if (b->cfg.kind == CH_WS2812 && b->cfg.words)
            anyWs = true;
    }
    if (anyWs && time_us_32() - doneUs < PBX_WS2812_LATCH_US) {
        stats.overDraw++;
        return;
    }

    channels_commit();

    uint32_t apaMaxBits = 0;
    uint32_t apaHz = 0;
    uint32_t anyWords = 0;
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        const ch_config_t *cfg = &channels[ch].draw->cfg;
        anyWords += cfg->words;
        if (cfg->kind == CH_APA102_DATA || cfg->kind == CH_APA102_CLOCK) {
            if (apaHz == 0 || cfg->frequency < apaHz)
                apaHz = cfg->frequency;
        }
        if (cfg->kind == CH_APA102_DATA && cfg->words * 32 > apaMaxBits)
            apaMaxBits = cfg->words * 32;
    }
    if (anyWords == 0)
        return;

    if (apaHz < PBX_APA102_MIN_HZ)
        apaHz = PBX_APA102_MIN_HZ;
    if (apaHz > PBX_APA102_MAX_HZ)
        apaHz = PBX_APA102_MAX_HZ;
    float apaDiv = (float) clock_get_hz(clk_sys) / (apaHz * 2.0f);

    uint32_t enableMask[2] = {0, 0};
    activeMask = 0;
    clockMask = 0;
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        const ch_buffer_t *b = channels[ch].draw;
        PIO pio = ch_pio(ch);
        uint sm = ch_sm(ch);
        pio_sm_set_enabled(pio, sm, false);
        pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
        switch (b->cfg.kind) {
        case CH_WS2812:
            if (!b->cfg.words)
                continue;
            setup_ws2812(ch, b);
            break;
        case CH_APA102_DATA:
            if (!b->cfg.words)
                continue;
            setup_apa102_data(ch, b, apaDiv);
            break;
        case CH_APA102_CLOCK:
            if (!apaMaxBits)
                continue;
            setup_apa102_clk(ch, apaMaxBits, apaDiv);
            clockMask |= 1u << ch;
            enableMask[ch / 4] |= 1u << sm;
            continue;
        default:
            continue;
        }
        activeMask |= 1u << ch;
        enableMask[ch / 4] |= 1u << sm;
    }

    busy = true;
    stats.drawCount++;
    stats.lastDrawMs = port_millis();
    // One CTRL write to PIO0 enables and restarts the dividers of its own
    // state machines and, through NEXT_PIO_MASK, PIO1's on the same cycle.
    pio_enable_sm_multi_mask_in_sync(pios[0], 0, enableMask[0], enableMask[1]);
}
