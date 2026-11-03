// Frame parser over a receive ring. Hunts for the magic, validates the record
// header, then streams the payload in chunks through the port hook, which
// copies and runs the CRC, into the channel being built. The CRC bytes at
// the end decide whether the frame becomes a draw candidate.

#include <string.h>
#include "parser.h"
#include "protocol.h"
#include "channels.h"

volatile stats_t stats;

// Payload moves through here in chunks. Divisible by every pixel size.
#define BOUNCE_LEN 1024
static uint8_t bounce[BOUNCE_LEN];

// The parser treats a nearly full ring as an overrun. It drains the ring
// every poll, so this much backlog means the DMA has lapped it.
#define RING_OVERRUN_MARGIN 64

void parser_init(parser_t *p, ring_t *ring) {
    memset(p, 0, sizeof(*p));
    p->ring = ring;
}

void parser_reset(parser_t *p) {
    p->inBody = false;
}

static void copy_peek(const ring_t *r, uint32_t start, void *dst, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++)
        d[i] = ring_peek(r, start + i);
}

// Record header length for a record type. False for an unknown type.
static bool record_sizes(uint8_t type, uint32_t *hdrLen, uint8_t *bpp) {
    switch (type) {
    case PB_SET_CHANNEL_WS2812:
        *hdrLen = sizeof(pb_ws2812_channel_t);
        *bpp = 3;  // corrected from the header after validation
        return true;
    case PB_DRAW_ALL:
        *hdrLen = 0;
        *bpp = 1;
        return true;
    case PB_SET_CHANNEL_APA102_DATA:
        *hdrLen = sizeof(pb_apa102_data_channel_t);
        *bpp = 4;
        return true;
    case PB_SET_CHANNEL_APA102_CLOCK:
        *hdrLen = sizeof(pb_apa102_clock_channel_t);
        *bpp = 1;
        return true;
    case PB_SET_CHANNEL_APA102_FAST_DATA:
        *hdrLen = sizeof(pb_apa102_fast_data_channel_t);
        *bpp = 2;
        return true;
    default:
        return false;
    }
}

// Validate a record header and return its payload length, or -1.
static int32_t payload_len(uint8_t type, const uint8_t *hdr) {
    switch (type) {
    case PB_SET_CHANNEL_WS2812: {
        const pb_ws2812_channel_t *h = (const void *) hdr;
        if (h->numElements < 3 || h->numElements > 4)
            return -1;
        uint32_t bytes = (uint32_t) h->pixels * h->numElements;
        if (bytes > PB_MAX_BYTES_PER_CHANNEL)
            return -1;
        return bytes;
    }
    case PB_DRAW_ALL:
        return 0;
    case PB_SET_CHANNEL_APA102_DATA: {
        const pb_apa102_data_channel_t *h = (const void *) hdr;
        if (h->frequency == 0)
            return -1;
        if (((uint32_t) h->pixels + 2) * 4 > PB_MAX_BYTES_PER_CHANNEL)
            return -1;
        return (uint32_t) h->pixels * 4;
    }
    case PB_SET_CHANNEL_APA102_CLOCK: {
        const pb_apa102_clock_channel_t *h = (const void *) hdr;
        if (h->frequency == 0)
            return -1;
        return 0;
    }
    case PB_SET_CHANNEL_APA102_FAST_DATA: {
        const pb_apa102_fast_data_channel_t *h = (const void *) hdr;
        if (h->frequency == 0)
            return -1;
        if (h->gammaTenths < 10 || h->gammaTenths > 25)
            return -1;
        if (((uint32_t) h->pixels + 2) * 4 > PB_MAX_BYTES_PER_CHANNEL)
            return -1;
        return (uint32_t) h->pixels * 2;
    }
    default:
        return -1;
    }
}

// Hunt for a frame start and validate its header. Returns true when the
// parser has entered the body state, false when it needs more bytes.
static bool start_frame(parser_t *p) {
    ring_t *r = p->ring;
    for (;;) {
        uint32_t avail = ring_available(r);
        if (avail < PB_FRAME_HEADER_LEN)
            return false;

        if (ring_peek(r, 0) != PB_MAGIC_0) {
            ring_skip(r, 1);
            stats.huntBytes++;
            continue;
        }
        if (ring_peek(r, 1) != PB_MAGIC_1 || ring_peek(r, 2) != PB_MAGIC_2 || ring_peek(r, 3) != PB_MAGIC_3) {
            ring_skip(r, 1);
            stats.frameMisses++;
            continue;
        }

        uint8_t channel = ring_peek(r, 4);
        uint8_t type = ring_peek(r, 5);
        uint32_t hdrLen;
        uint8_t bpp;
        if (!record_sizes(type, &hdrLen, &bpp)) {
            ring_skip(r, 1);
            stats.badHeaders++;
            continue;
        }
        if (avail < PB_FRAME_HEADER_LEN + hdrLen)
            return false;

        uint8_t hdr[PB_MAX_RECORD_HEADER_LEN];
        copy_peek(r, PB_FRAME_HEADER_LEN, hdr, hdrLen);
        int32_t payload = payload_len(type, hdr);
        if (payload < 0) {
            ring_skip(r, 1);
            stats.badHeaders++;
            continue;
        }
        if (type == PB_SET_CHANNEL_WS2812)
            bpp = ((const pb_ws2812_channel_t *) hdr)->numElements;

        stats.lastDataMs = port_millis();

        uint32_t hdrTotal = PB_FRAME_HEADER_LEN + hdrLen;
        p->mine = type == PB_DRAW_ALL || (channel >> 3) == port_bus_id();
        if (p->mine) {
            p->crc = port_copy_crc(r, r->rd, hdrTotal, bounce, true);
            channels_begin(type, hdr);
        } else {
            stats.otherBoard++;
        }
        ring_skip(r, hdrTotal);
        p->channel = channel & 7;
        p->type = type;
        p->bpp = bpp;
        p->remaining = payload;
        p->inBody = true;
        return true;
    }
}

void parser_poll(parser_t *p) {
    ring_t *r = p->ring;
    for (;;) {
        uint32_t avail = ring_available(r);
        if (avail > r->mask - RING_OVERRUN_MARGIN) {
            stats.rxOverruns++;
            r->rd = r->wr;
            p->inBody = false;
            return;
        }

        if (!p->inBody) {
            if (!start_frame(p))
                return;
            continue;
        }

        if (p->remaining) {
            uint32_t n = avail < p->remaining ? avail : p->remaining;
            if (n > BOUNCE_LEN)
                n = BOUNCE_LEN;
            n -= n % p->bpp;
            if (n == 0)
                return;
            if (p->mine) {
                p->crc = port_copy_crc(r, r->rd, n, bounce, false);
                channels_pixels(bounce, n / p->bpp);
            }
            ring_skip(r, n);
            p->remaining -= n;
            continue;
        }

        if (avail < PB_CRC_LEN)
            return;
        uint32_t crcRead;
        copy_peek(r, 0, &crcRead, PB_CRC_LEN);
        ring_skip(r, PB_CRC_LEN);
        p->inBody = false;

        if (!p->mine)
            continue;
        if (p->crc != crcRead) {
            stats.crcErrors++;
            continue;
        }
        if (p->type == PB_DRAW_ALL) {
            port_draw_all();
        } else {
            channels_end(p->channel, true);
            stats.lastValidChannelMs = port_millis();
        }
    }
}
