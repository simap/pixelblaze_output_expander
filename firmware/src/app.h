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
#define PBX_BAUD_SM 2

// Auto baud. The rate is held while valid channel frames arrive. After
// PBX_BAUD_LOCK_MS without one, measurements are collected for
// PBX_BAUD_WINDOW_MS and the fastest cluster with at least two samples is
// taken, since slower clusters are data patterns that look like a 'U' at a
// multiple of the bit period. The divider is rewritten when the result
// differs from the current rate by more than PBX_BAUD_CHANGE_PCT, then
// nothing changes for PBX_BAUD_HOLD_MS to let frames lock it.
#define PBX_BAUD_LOCK_MS 200
#define PBX_BAUD_WINDOW_MS 50
#define PBX_BAUD_HOLD_MS 150
#define PBX_BAUD_CHANGE_PCT 1.0f
#define PBX_BAUD_CLUSTER_PCT 3.0f
#define PBX_BAUD_MIN 100000
#define PBX_BAUD_MAX 9000000

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

// Update payload region, overlaid on the pixel buffer pool. One flash slot,
// which is the most a payload can be. Page aligned, so a payload that fits
// also fits with its page padding.
#define PBX_UPDATE_REGION_BYTES (384 * 1024)

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
    // Update fields are appended so the loopback test's layout stays valid.
    uint16_t updateOther;     // update records for other keys, CRC checked
    uint8_t updateState;      // enum pbx_update_state
    uint8_t updateRefused;    // enum pbx_update_refused, last refusal of a matching key
    uint32_t updateBytes;     // payload bytes received so far
    uint8_t bootType;         // bootrom BOOT_TYPE_ of this boot
    int8_t bootPartition;     // partition booted, -1 when none
    uint8_t activation;       // enum pbx_activation
    int32_t buyResult;        // rom_explicit_buy return, 0 before it runs
    // Auto baud, appended for the same reason.
    uint16_t baudChanges;     // UART divider rewrites from measurement
    uint16_t baudSamples;     // valid 'U' measurements seen
    uint32_t baudMeasured;    // last accepted rate before snapping
    uint32_t baudRaw1;        // last valid pair: start bit iterations
    uint32_t baudRaw2;        // last valid pair: 8 bit span iterations
} stats_t;

extern volatile stats_t stats;

enum pbx_update_state {
    PBX_UPDATE_IDLE = 0,
    PBX_UPDATE_RECEIVING,
    PBX_UPDATE_FLASHING,
};

// Why an update with a matching key was refused before the point of no
// return. Everything past that point ends in a reboot.
enum pbx_update_refused {
    PBX_REFUSED_NONE = 0,
    PBX_REFUSED_NO_SLOT,      // not running from an A/B partition pair
    PBX_REFUSED_TOO_BIG,      // larger than the update region or the slot
    PBX_REFUSED_UNBOUGHT,     // arrived before the buy, which it triggered instead
};

// TBYB buy state of this boot.
enum pbx_activation {
    PBX_ACTIVATION_NONE = 0,  // no buy pending, nothing to do
    PBX_ACTIVATION_PENDING,   // waits for the first frame with a good CRC
    PBX_ACTIVATION_DONE,      // bought
    PBX_ACTIVATION_FAILED,    // buy returned an error, see buyResult
};

// Hooks the device or the host test provides.

// Copy len bytes starting at ring index start into dst and return the CRC32
// so far. first restarts the CRC at this chunk, otherwise it continues from
// the previous call.
uint32_t port_copy_crc(const ring_t *ring, uint32_t start, uint32_t len, uint8_t *dst, bool first);

void port_draw_all(void);
uint8_t port_bus_id(void);
uint32_t port_millis(void);

// The one update key this build accepts, from the board header.
const char *port_update_key(void);

// Called once per frame whose CRC the parser checked and found good, after
// the frame has been acted on.
void port_frame_valid(void);

// Point of no return for an update record whose key and header CRC
// matched. Returns the RAM region the payload streams into, or NULL if the
// update is refused, in which case the body is CRC checked and discarded.
// After a non NULL return the LED output is stopped and the only exits are
// port_update_apply and port_update_abort. On the device neither returns.
uint8_t *port_update_begin(uint32_t len);

// The payload is complete and its frame CRC passed.
void port_update_apply(uint8_t *payload, uint32_t len);

// The frame CRC failed or the input stopped before the payload was complete.
void port_update_abort(void);

#endif
