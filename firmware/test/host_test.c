// Host test for the parser and channel conversion. Builds the pure modules
// with the host compiler and feeds synthetic frames through a ring.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app.h"
#include "parser.h"
#include "channels.h"
#include "crc32.h"
#include "protocol.h"
#include "gamma.h"
#include <math.h>

// R to position 0, G to 1, B to 2, W to 3
#define ORDER_RGB (0 | (1 << 2) | (2 << 4) | (3 << 6))

// gamma.cpp needs constexpr pow, which the host clang does not provide.
// Build the same tables at runtime.
static uint16_t gammaStore[16][256];
const uint16_t *gammaTables[16];

static void gamma_init(void) {
    for (int t = 0; t < 16; t++) {
        for (int v = 0; v < 256; v++) {
            double v16 = pow(v / 255.0, (t + 10) / 10.0) * 65535.0 + 0.5;
            if (v16 > 65535.0) v16 = 65535.0;
            gammaStore[t][v] = (uint16_t) v16;
        }
        gammaTables[t] = gammaStore[t];
    }
}

#define RING_BITS 13
#define RING_SIZE (1u << RING_BITS)
static uint8_t ringBuf[RING_SIZE];
static ring_t ring = { ringBuf, RING_SIZE - 1, RING_BITS, 0, 0 };

static uint8_t busId = 0;
static uint32_t millis = 1000;
static int drawCalls;

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static parser_t parser;
static uint32_t runningCrc;

uint32_t port_copy_crc(const ring_t *r, uint32_t start, uint32_t len, uint8_t *dst, bool first) {
    for (uint32_t i = 0; i < len; i++)
        dst[i] = r->buf[(start + i) & r->mask];
    if (first)
        runningCrc = 0xffffffffu;
    runningCrc = crc32_update(runningCrc, dst, len);
    return runningCrc ^ 0xffffffffu;
}

void port_draw_all(void) {
    drawCalls++;
    channels_commit();
}

uint8_t port_bus_id(void) { return busId; }
uint32_t port_millis(void) { return millis; }

// Push bytes into the ring the way the DMA would.
static void feed(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ringBuf[ring.wr] = data[i];
        ring.wr = (ring.wr + 1) & ring.mask;
    }
}

// Build a frame into out. Returns its length.
static uint32_t build(uint8_t *out, uint8_t channel, uint8_t type, const void *hdr, uint32_t hdrLen, const uint8_t *payload, uint32_t payloadLen) {
    uint32_t n = 0;
    out[n++] = 'U'; out[n++] = 'P'; out[n++] = 'X'; out[n++] = 'L';
    out[n++] = channel;
    out[n++] = type;
    memcpy(out + n, hdr, hdrLen); n += hdrLen;
    memcpy(out + n, payload, payloadLen); n += payloadLen;
    uint32_t crc = crc32_buf(out, n);
    memcpy(out + n, &crc, 4); n += 4;
    return n;
}

static uint8_t frame[PB_MAX_FRAME_LEN + 64];

static void reset(void) {
    memset(&ring, 0, sizeof(ring));
    ring.buf = ringBuf;
    ring.mask = RING_SIZE - 1;
    ring.sizeBits = RING_BITS;
    memset((void *) &stats, 0, sizeof(stats));
    channels_init();
    parser_init(&parser, &ring);
    drawCalls = 0;
}

static void test_ws2812_and_draw(void) {
    reset();
    // order GRB: R goes to position 1, G to 0, B to 2
    pb_ws2812_channel_t h = { 3, (1 << 0) | (0 << 2) | (2 << 4), 3 };
    uint8_t px[9] = { 1, 2, 3, 10, 20, 30, 100, 200, 250 };
    uint32_t n = build(frame, 2, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 9);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(channels[2].candidate);
    CHECK(channels[2].candidate->cfg.kind == CH_WS2812);
    CHECK(channels[2].candidate->cfg.words == 3);
    CHECK(channels[2].candidate->cfg.bitsPerWord == 24);
    CHECK(channels[2].candidate->data[0] == 0x02010300);
    CHECK(channels[2].candidate->data[2] == 0xc864fa00);
    CHECK(stats.lastValidChannelMs == millis);

    n = build(frame, 0, PB_DRAW_ALL, NULL, 0, NULL, 0);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(drawCalls == 1);
    CHECK(!channels[2].candidate);
    CHECK(channels[2].draw->data[0] == 0x02010300);
    CHECK(stats.crcErrors == 0);
    CHECK(ring_available(&ring) == 0);
}

static void test_bad_crc_keeps_front(void) {
    reset();
    pb_ws2812_channel_t h = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 9, 9, 9 };
    uint32_t n = build(frame, 0, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 3);
    feed(frame, n);
    n = build(frame, 0, PB_DRAW_ALL, NULL, 0, NULL, 0);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(channels[0].draw->data[0] == 0x09090900);

    uint8_t px2[3] = { 1, 1, 1 };
    n = build(frame, 0, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px2, 3);
    frame[10] ^= 0xff;  // corrupt a payload byte
    feed(frame, n);
    n = build(frame, 0, PB_DRAW_ALL, NULL, 0, NULL, 0);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(stats.crcErrors == 1);
    CHECK(drawCalls == 2);
    CHECK(channels[0].draw->data[0] == 0x09090900);
    CHECK(!channels[0].candidate);
}

static void test_resync_and_garbage(void) {
    reset();
    uint8_t junk[] = { 0x00, 'U', 'U', 'P', 'X', 'L', 0x55, 0x55 };
    feed(junk, 2);  // "\0U" then the real frame starts with its own U
    pb_ws2812_channel_t h = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 4, 5, 6 };
    uint32_t n = build(frame, 0, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(channels[0].candidate);
    CHECK(channels[0].candidate->data[0] == 0x04050600);
    CHECK(stats.huntBytes == 1);
    CHECK(stats.frameMisses == 1);

    // Unknown record type and invalid header both resume hunting at the next byte.
    reset();
    n = build(frame, 0, 99, NULL, 0, NULL, 0);
    feed(frame, n);
    pb_ws2812_channel_t bad = { 7, 0, 1 };
    n = build(frame, 0, PB_SET_CHANNEL_WS2812, &bad, sizeof(bad), px, 3);
    feed(frame, n);
    n = build(frame, 0, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(stats.badHeaders == 2);
    CHECK(channels[0].candidate);
}

static void test_other_board(void) {
    reset();
    busId = 3;
    pb_ws2812_channel_t h = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 4, 5, 6 };
    uint32_t n = build(frame, (2 << 3) | 1, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 3);
    feed(frame, n);
    n = build(frame, (3 << 3) | 1, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(stats.otherBoard == 1);
    CHECK(channels[1].candidate);
    CHECK(ring_available(&ring) == 0);
    busId = 0;
}

static void test_partial_and_wrap(void) {
    reset();
    pb_ws2812_channel_t h = { 4, (0) | (1 << 2) | (2 << 4) | (3 << 6), 400 };
    uint8_t px[1600];
    for (int i = 0; i < 1600; i++)
        px[i] = i & 0xff;
    uint32_t n = build(frame, 5, PB_SET_CHANNEL_WS2812, &h, sizeof(h), px, 1600);

    // Place the frame so it straddles the end of the ring.
    ring.rd = ring.wr = RING_SIZE - 100;
    feed(frame, 50);
    parser_poll(&parser);
    CHECK(!channels[5].candidate);
    CHECK(ring_available(&ring) == 0);  // header and the first 10 pixels streamed in
    feed(frame + 50, n - 50);
    parser_poll(&parser);
    CHECK(channels[5].candidate);
    CHECK(channels[5].candidate->cfg.words == 400);
    CHECK(channels[5].candidate->cfg.bitsPerWord == 32);
    CHECK(channels[5].candidate->data[0] == 0x00010203);
    CHECK(channels[5].candidate->data[399] == 0x3c3d3e3f);
    CHECK(ring_available(&ring) == 0);
}

static void test_apa102(void) {
    reset();
    pb_apa102_data_channel_t h = { 2000000, ORDER_RGB, 0, 2 };
    uint8_t px[8] = { 1, 2, 3, 31, 4, 5, 6, 1 };
    uint32_t n = build(frame, 6, PB_SET_CHANNEL_APA102_DATA, &h, sizeof(h), px, 8);
    feed(frame, n);
    pb_apa102_clock_channel_t c = { 2000000 };
    n = build(frame, 7, PB_SET_CHANNEL_APA102_CLOCK, &c, sizeof(c), NULL, 0);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(channels[6].candidate);
    CHECK(channels[6].candidate->cfg.kind == CH_APA102_DATA);
    CHECK(channels[6].candidate->cfg.words == 4);
    CHECK(channels[6].candidate->cfg.frequency == 2000000);
    CHECK(channels[6].candidate->data[0] == 0);
    CHECK(channels[6].candidate->data[1] == 0xff010203);
    CHECK(channels[6].candidate->data[2] == 0xe1040506);
    CHECK(channels[6].candidate->data[3] == 0xff000000);
    CHECK(channels[7].candidate);
    CHECK(channels[7].candidate->cfg.kind == CH_APA102_CLOCK);
    CHECK(channels[7].candidate->cfg.words == 0);

    // Fast data: white at gamma 1.0 lands at brightness 31, colour 254.
    pb_apa102_fast_data_channel_t f = { 2000000, ORDER_RGB, 10, 2 };
    uint8_t fpx[4] = { 0xff, 0xff, 0x00, 0x00 };
    n = build(frame, 4, PB_SET_CHANNEL_APA102_FAST_DATA, &f, sizeof(f), fpx, 4);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(channels[4].candidate);
    CHECK(channels[4].candidate->cfg.words == 4);
    CHECK(channels[4].candidate->data[1] == 0xfffefefe);
    CHECK(channels[4].candidate->data[2] == 0xe0000000);
}

int main(void) {
    gamma_init();
    test_ws2812_and_draw();
    test_bad_crc_keeps_front();
    test_resync_and_garbage();
    test_other_board();
    test_partial_and_wrap();
    test_apa102();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
