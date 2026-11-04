// Auto baud. The baud_measure program pushes two loop iteration counts per
// byte it catches on the data input: the width of the low run it started
// on and the span to the fourth falling edge after it. For a 'U' (0x55)
// caught at its start bit the span is 8 low widths. The program catches
// any falling edge, so some data patterns pass the same ratio at 2 or 3
// times the bit period. The real 'U' is always the fastest cluster in a
// window, which is what gets used.
//
// Loop iterations are 2 cycles. The program's branch paths add a fixed
// number of cycles that do not scale with the rate, measured against a
// Pixelblaze at 4 and 5 Mbaud and subtracted here.

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "autobaud.h"
#include "app.h"
#include "outputexpander.pio.h"

// cycles = 2 * iterations + offset
#define START_OFFSET 2
#define SPAN_OFFSET 12

static PIO pio;
static uint sm;

static uint32_t prev;
static bool havePrev;

// Fastest cluster in the current window.
static float clusterBit;
static int clusterCount;
static uint32_t windowStartMs;
static uint32_t holdUntilMs;

void autobaud_init(void) {
    pio = pio2;
    sm = PBX_BAUD_SM;
    uint offset = pio_add_program(pio, &baud_measure_program);
    pio_sm_config c = baud_measure_program_get_default_config(offset);
    sm_config_set_in_pins(&c, PBX_DATA_IN_UART_PIN);
    sm_config_set_jmp_pin(&c, PBX_DATA_IN_UART_PIN);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
    windowStartMs = port_millis();
}

static void sample(float bitCycles) {
    if (clusterCount == 0 || bitCycles < clusterBit * (1.0f - PBX_BAUD_CLUSTER_PCT / 100.0f)) {
        clusterBit = bitCycles;
        clusterCount = 1;
    } else if (bitCycles < clusterBit * (1.0f + PBX_BAUD_CLUSTER_PCT / 100.0f)) {
        clusterBit = (clusterBit * clusterCount + bitCycles) / (clusterCount + 1);
        clusterCount++;
    }
}

static void window_reset(uint32_t now) {
    clusterCount = 0;
    windowStartMs = now;
}

void autobaud_poll(void) {
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        uint32_t v = pio_sm_get(pio, sm);
        if (havePrev) {
            uint32_t start = 2 * prev + START_OFFSET;
            uint32_t span = 2 * v + SPAN_OFFSET;
            // 'U' test: span within 8 percent of 8 low widths
            if (span * 100 > start * 8 * 92 && span * 100 < start * 8 * 108) {
                stats.baudSamples++;
                stats.baudRaw1 = prev;
                stats.baudRaw2 = v;
                float bitCycles = (float) span / 8.0f;
                float rate = (float) clock_get_hz(clk_sys) / bitCycles;
                if (rate >= PBX_BAUD_MIN && rate <= PBX_BAUD_MAX)
                    sample(bitCycles);
            }
        }
        prev = v;
        havePrev = true;
    }

    uint32_t now = port_millis();
    if (now - stats.lastValidChannelMs < PBX_BAUD_LOCK_MS) {
        window_reset(now);
        return;
    }
    if ((int32_t) (now - holdUntilMs) < 0)
        return;
    if (now - windowStartMs < PBX_BAUD_WINDOW_MS)
        return;

    if (clusterCount >= 2) {
        float rate = (float) clock_get_hz(clk_sys) / clusterBit;
        stats.baudMeasured = (uint32_t) rate;
        float current = (float) stats.baud;
        if (rate < current * (1.0f - PBX_BAUD_CHANGE_PCT / 100.0f) || rate > current * (1.0f + PBX_BAUD_CHANGE_PCT / 100.0f)) {
            stats.baud = uart_set_baudrate(uart0, (uint) rate);
            stats.baudChanges++;
            holdUntilMs = now + PBX_BAUD_HOLD_MS;
        }
    }
    window_reset(now);
}
