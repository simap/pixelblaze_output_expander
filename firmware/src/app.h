// Shared definitions for the output expander. Nothing here depends on the
// pico-sdk, so the parser and channel code build on a host for tests.

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

#define PBX_CHANNELS 8

// Default UART rate. The Pixelblaze side also supports 3, 4, and 5 Mbaud.
#define PBX_UART_BAUD 2000000

// SPI input on a PIO receiver with no select line. Clock idles low. CPHA 0
// samples on the rising edge, 1 on the falling edge. Clock rate 500 kHz to
// 20 MHz with an asynchronous sender.
#define PBX_SPI_CPHA 0

// PIO2 state machines. PIO0 and PIO1 are the eight output channels.
#define PBX_LOOPBACK_SM 0
#define PBX_SPI_RX_SM 1

// After this long with no clock edge the receiver clears any partial byte.
// Never fires inside a byte from a hardware sender at 200 kHz or above. A
// sender blank of at least 20 us before each frame set guarantees a
// realign after a lost or extra clock edge.
#define PBX_SPI_IDLE_US 10

// SPI ring counts as live after this many bytes, and the parser returns to
// the UART ring after the SPI ring has been quiet this long. Long enough
// that a render stall inside a frame does not drop the frame.
#define PBX_SPI_LIVE_BYTES 8
#define PBX_SPI_HOLD_MS 1000

// WS2812 output is fixed at 800 kHz. Latch time is counted from the end of
// the last channel's data.
#define PBX_WS2812_BIT_HZ 800000
#define PBX_WS2812_LATCH_US 300

// APA102 clock range. Every APA102 channel in a draw runs at the lowest
// frequency requested among them, since clock and data state machines share
// one divider setting to stay in lockstep.
#define PBX_APA102_MIN_HZ 10000
#define PBX_APA102_MAX_HZ 20000000

// LED PWM levels out of 0xffff, same as the v3 expander.
#define PBX_STATUS_LED_BRIGHTNESS (0x7ff / 4)
#define PBX_DRAW_LED_BRIGHTNESS (0x3ff / 4)

// Receive ring. A byte ring filled by DMA, consumed by the parser. buf is
// aligned to its size so the DMA ring wrap works on both fill and copy.
typedef struct {
    uint8_t *buf;
    uint32_t mask;      // size - 1
    uint32_t sizeBits;  // log2 of size
    uint32_t rd;        // next byte for the parser
    uint32_t wr;        // next byte the DMA writes, refreshed before each poll
} ring_t;

static inline uint32_t ring_available(const ring_t *r) {
    return (r->wr - r->rd) & r->mask;
}

static inline uint8_t ring_peek(const ring_t *r, uint32_t i) {
    return r->buf[(r->rd + i) & r->mask];
}

static inline void ring_skip(ring_t *r, uint32_t n) {
    r->rd = (r->rd + n) & r->mask;
}

enum pbx_input {
    PBX_INPUT_UART = 0,
    PBX_INPUT_SPI = 1,
};

// Counters readable over SWD while running. One global, never static.
typedef struct {
    uint32_t uartBytes;
    uint32_t spiBytes;
    uint32_t huntBytes;      // bytes skipped looking for the magic
    uint16_t frameMisses;    // 'U' not followed by "PXL"
    uint16_t badHeaders;     // record header failed validation
    uint16_t crcErrors;
    uint16_t otherBoard;     // frames addressed to another bus id
    uint16_t drawCount;
    uint16_t overDraw;       // DRAW_ALL while output busy or inside the latch window
    uint16_t rxOverruns;     // ring filled faster than the parser drained it
    uint16_t uartErrors;     // framing, parity, break, or overrun flagged by the UART
    uint8_t input;           // enum pbx_input
    uint8_t busId;
    uint32_t baud;
    uint32_t crcSelfTest;    // sniffer CRC32 of "123456789", expect 0xcbf43926
    uint32_t crcSelfTestSw;  // software CRC32 of the same
    uint32_t lastDataMs;
    uint32_t lastValidChannelMs;
    uint32_t lastDrawMs;
} stats_t;

extern volatile stats_t stats;

// Hooks the device or the host test provides.

// Copy len bytes starting at ring index start into dst and return the CRC32
// so far. first restarts the CRC at this chunk, otherwise it continues from
// the previous call.
uint32_t port_copy_crc(const ring_t *ring, uint32_t start, uint32_t len, uint8_t *dst, bool first);

void port_draw_all(void);
uint8_t port_bus_id(void);
uint32_t port_millis(void);

#endif
