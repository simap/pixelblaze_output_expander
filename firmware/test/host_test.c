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
#include "imagedef.h"
#include <math.h>
#include <stddef.h>

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

// Update hooks. The update region is a test buffer, and apply and abort
// record the call instead of rebooting.
#define UPDATE_MAX 65536
static uint8_t updateRegion[UPDATE_MAX];
static int validFrames;
static int beginCalls;
static int abortCalls;
static struct {
    int calls;
    uint8_t *payload;
    uint32_t len;
    int validFramesAtEnd;
} updateApply;

#define TEST_KEY "RP2350_ARM-2MB-standard-firmware"
const char *port_update_key(void) { return TEST_KEY; }

void port_frame_valid(void) { validFrames++; }

uint8_t *port_update_begin(uint32_t len) {
    beginCalls++;
    if (len > UPDATE_MAX)
        return NULL;
    return updateRegion;
}

void port_update_apply(uint8_t *payload, uint32_t len) {
    updateApply.calls++;
    updateApply.payload = payload;
    updateApply.len = len;
    updateApply.validFramesAtEnd = validFrames;
}

void port_update_abort(void) { abortCalls++; }

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
    validFrames = 0;
    beginCalls = 0;
    abortCalls = 0;
    memset(&updateApply, 0, sizeof(updateApply));
}

// Update record header with its CRC filled in.
static pb_update_t update_hdr(const char *key, uint32_t length) {
    pb_update_t h;
    memset(&h, 0, sizeof(h));
    strncpy(h.key, key, PB_UPDATE_KEY_LEN);
    h.length = length;
    h.crc = crc32_buf((const uint8_t *) &h, offsetof(pb_update_t, crc));
    return h;
}

// A minimal image: a block loop of one IMAGE_DEF block with a version and
// a hash, at offset 256, with filler either side that includes the frame
// magic.
static uint32_t build_firmware(uint8_t *out, uint32_t len, uint16_t major, uint16_t minor, uint8_t hashSeed) {
    for (uint32_t i = 0; i < len; i++)
        out[i] = (uint8_t) (i * 7 + hashSeed);
    memcpy(out + 40, "UPXL", 4);
    uint32_t *w = (uint32_t *) (out + 256);
    int n = 0;
    w[n++] = 0xffffded3;                       // start marker
    w[n++] = 0x42 | (1 << 8) | (0x1021 << 16);  // IMAGE_TYPE, 1 word, EXE ARM RP2350
    w[n++] = 0x48 | (2 << 8);                   // VERSION, 2 words
    w[n++] = minor | ((uint32_t) major << 16);
    w[n++] = 0x4b | (9 << 8);                   // HASH_VALUE, 9 words
    for (int i = 0; i < 8; i++)
        w[n++] = 0x01010101u * (hashSeed + i);
    w[n++] = 0xff | (12 << 8);                  // LAST, 12 words of items
    w[n++] = 0;                                 // link to self
    w[n++] = 0xab123579;                        // end marker
    memcpy(out + 256 + n * 4 + 8, "UPXL", 4);
    return len;
}

static uint8_t firmware[4096];
static uint8_t bigFrame[sizeof(firmware) + 128];

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

static void test_update_received(void) {
    reset();
    uint32_t len = build_firmware(firmware,3000, 4, 1, 1);
    pb_update_t h = update_hdr(TEST_KEY,len);
    uint32_t n = build(bigFrame, 0x3f, PB_UPDATE, &h, sizeof(h), firmware,len);

    // Feed in pieces so the body streams in chunks.
    feed(bigFrame, 100);
    parser_poll(&parser);
    CHECK(beginCalls == 1);
    CHECK(stats.updateState == PBX_UPDATE_RECEIVING);
    CHECK(updateApply.calls == 0);
    feed(bigFrame + 100, n - 100);
    parser_poll(&parser);
    CHECK(updateApply.calls == 1);
    CHECK(abortCalls == 0);
    CHECK(updateApply.payload == updateRegion);
    CHECK(updateApply.len == len);
    CHECK(stats.updateBytes == len);
    CHECK(memcmp(updateRegion, firmware, len) == 0);
    // The frame counts as valid after it is acted on. On the device apply
    // never returns, and the buy happened at the header.
    CHECK(validFrames == 1);
    CHECK(updateApply.validFramesAtEnd == 0);
    CHECK(stats.crcErrors == 0);
    CHECK(ring_available(&ring) == 0);

    imagedef_t d;
    imagedef_read(updateRegion, len, &d);
    CHECK(d.found);
    CHECK(d.hasHash && d.hash[0] == 1 && d.hash[4] == 2 && d.hash[31] == 8);
}

static void test_update_other_key(void) {
    reset();
    uint32_t len = build_firmware(firmware,2000, 1, 0, 9);
    pb_update_t h = update_hdr("RP2350_ARM-2MB-other-firmware", len);
    uint32_t n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,len);
    feed(bigFrame, n);
    pb_ws2812_channel_t w = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 4, 5, 6 };
    n = build(frame, 1, PB_SET_CHANNEL_WS2812, &w, sizeof(w), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    // Consumed with its CRC checked, so it counts as a valid frame, and
    // the pixel frame after it parsed normally.
    CHECK(beginCalls == 0);
    CHECK(updateApply.calls == 0 && abortCalls == 0);
    CHECK(stats.updateOther == 1);
    CHECK(stats.updateState == PBX_UPDATE_IDLE);
    CHECK(validFrames == 2);
    CHECK(channels[1].candidate);
    CHECK(stats.crcErrors == 0);

    // Too big for the region takes the same path.
    reset();
    h = update_hdr(TEST_KEY,UPDATE_MAX + 1);
    n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,0);
    feed(bigFrame, n - 4);  // header only, the body would be huge
    parser_poll(&parser);
    CHECK(beginCalls == 1);
    CHECK(stats.updateOther == 1);
    CHECK(parser.body == BODY_CRC_ONLY);
}

static void test_update_bad_header(void) {
    reset();
    uint32_t len = build_firmware(firmware,1000, 1, 0, 3);
    pb_update_t h = update_hdr(TEST_KEY,len);
    h.length ^= 0x100;  // header CRC no longer matches
    uint32_t n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,len);
    feed(bigFrame, n);
    pb_ws2812_channel_t w = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 4, 5, 6 };
    n = build(frame, 1, PB_SET_CHANNEL_WS2812, &w, sizeof(w), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    // The hunt also trips over the magic inside the payload.
    CHECK(beginCalls == 0);
    CHECK(stats.badHeaders >= 1);
    CHECK(channels[1].candidate);

    // A length over the cap is a bad header too.
    reset();
    h = update_hdr(TEST_KEY,PB_UPDATE_MAX_LEN + 1);
    n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,0);
    feed(bigFrame, n);
    parser_poll(&parser);
    CHECK(beginCalls == 0);
    CHECK(stats.badHeaders == 1);
}

static void test_update_bad_crc(void) {
    reset();
    uint32_t len = build_firmware(firmware,1500, 1, 0, 5);
    pb_update_t h = update_hdr(TEST_KEY,len);
    uint32_t n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,len);
    bigFrame[n - 10] ^= 1;
    feed(bigFrame, n);
    parser_poll(&parser);
    CHECK(beginCalls == 1);
    CHECK(abortCalls == 1);
    CHECK(updateApply.calls == 0);
    CHECK(stats.crcErrors == 1);
    CHECK(validFrames == 0);
}

static void test_update_reset(void) {
    // A ring switch mid payload aborts the update.
    reset();
    uint32_t len = build_firmware(firmware,1500, 1, 0, 5);
    pb_update_t h = update_hdr(TEST_KEY,len);
    uint32_t n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,len);
    feed(bigFrame, n / 2);
    parser_poll(&parser);
    CHECK(beginCalls == 1);
    CHECK(abortCalls == 0);
    parser_reset(&parser);
    CHECK(abortCalls == 1);
    CHECK(updateApply.calls == 0);
    CHECK(!parser.inBody);
}

static void test_update_magic_inside(void) {
    // The payload carries the magic twice. In sync, the body is consumed by
    // length. Out of sync, the parser lands on the embedded magic, finds no
    // valid header, and recovers at the next real frame.
    reset();
    uint32_t len = build_firmware(firmware,2000, 1, 0, 7);
    pb_update_t h = update_hdr(TEST_KEY,len);
    uint32_t n = build(bigFrame, 0, PB_UPDATE, &h, sizeof(h), firmware,len);
    feed(bigFrame, n);
    parser_poll(&parser);
    CHECK(updateApply.calls == 1 && abortCalls == 0);
    CHECK(stats.badHeaders == 0);

    reset();
    feed(bigFrame + 30, n - 30);  // powered on inside the body
    pb_ws2812_channel_t w = { 3, ORDER_RGB, 1 };
    uint8_t px[3] = { 4, 5, 6 };
    n = build(frame, 1, PB_SET_CHANNEL_WS2812, &w, sizeof(w), px, 3);
    feed(frame, n);
    parser_poll(&parser);
    CHECK(beginCalls == 0);
    CHECK(updateApply.calls == 0 && abortCalls == 0);
    CHECK(channels[1].candidate);
}

static void test_imagedef_loop(void) {
    // Two blocks: a bare one at the front linking to a sealed one at the
    // end that links back. The last IMAGE_DEF wins.
    static uint8_t img[32768];
    memset(img, 0xee, sizeof(img));
    uint32_t *a = (uint32_t *) (img + 0x138);
    a[0] = 0xffffded3;
    a[1] = 0x42 | (1 << 8) | (0x1021 << 16);
    a[2] = 0xff | (1 << 8);
    a[3] = 0x7000 - 0x138;
    a[4] = 0xab123579;
    uint32_t *b = (uint32_t *) (img + 0x7000);
    int n = 0;
    b[n++] = 0xffffded3;
    b[n++] = 0x42 | (1 << 8) | (0x1021 << 16);
    b[n++] = 0x48 | (2 << 8);
    b[n++] = 3 | (2u << 16);
    b[n++] = 0x4b | (9 << 8);
    for (int i = 0; i < 8; i++)
        b[n++] = 0xa0 + i;
    b[n++] = 0xff | (12 << 8);
    b[n++] = (uint32_t) (0x138 - 0x7000);
    b[n++] = 0xab123579;
    imagedef_t d;
    imagedef_read(img, sizeof(img), &d);
    CHECK(d.found);
    CHECK(d.hasHash && d.hash[0] == 0xa0 && d.hash[28] == 0xa7);

    // A truncated firmware keeps the bare front block, which has no hash.
    imagedef_read(img, 0x7010, &d);
    CHECK(d.found);
    CHECK(!d.hasHash);

    // No block at all.
    memset(img, 0, 4096);
    imagedef_read(img, sizeof(img), &d);
    CHECK(!d.found);
}

int main(void) {
    gamma_init();
    test_update_received();
    test_update_other_key();
    test_update_bad_header();
    test_update_bad_crc();
    test_update_reset();
    test_update_magic_inside();
    test_imagedef_loop();
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
