#include <string.h>
#include "channels.h"
#include "app.h"
#include "gamma.h"

channel_t channels[PBX_CHANNELS];

// The pool overlays the update region. See channels_update_region.
static union {
    ch_buffer_t pool[PBX_POOL_SIZE];
    uint8_t update[PBX_UPDATE_REGION_BYTES];
} region __attribute__((aligned(4)));
_Static_assert(sizeof(region.pool) <= PBX_UPDATE_REGION_BYTES, "update region smaller than the pool");
#define pool region.pool
static ch_buffer_t *freeList[PBX_POOL_SIZE];
static int freeCount;
static ch_buffer_t *rx;

// Frame in progress.
static struct {
    uint8_t type;
    uint8_t pos[4];           // output byte position of wire element 0 to 3
    const uint16_t *gamma;    // fast data only
    uint32_t index;           // next word to write
    ch_config_t cfg;
} build;

// Global brightness scale for APA102 fast data, indexed by the 5 bit
// brightness. Scales a 16 bit colour so that colour * gb / 31 is preserved.
#define HDR_ADJUST(gb) ((255 * 31) / (gb))
static const uint16_t hdrAdjustTable[32] = {
    0, HDR_ADJUST(1), HDR_ADJUST(2), HDR_ADJUST(3), HDR_ADJUST(4), HDR_ADJUST(5),
    HDR_ADJUST(6), HDR_ADJUST(7), HDR_ADJUST(8), HDR_ADJUST(9), HDR_ADJUST(10), HDR_ADJUST(11),
    HDR_ADJUST(12), HDR_ADJUST(13), HDR_ADJUST(14), HDR_ADJUST(15), HDR_ADJUST(16),
    HDR_ADJUST(17), HDR_ADJUST(18), HDR_ADJUST(19), HDR_ADJUST(20), HDR_ADJUST(21),
    HDR_ADJUST(22), HDR_ADJUST(23), HDR_ADJUST(24), HDR_ADJUST(25), HDR_ADJUST(26),
    HDR_ADJUST(27), HDR_ADJUST(28), HDR_ADJUST(29), HDR_ADJUST(30), HDR_ADJUST(31),
};

static ch_buffer_t *take(void) {
    return freeList[--freeCount];
}

static void give(ch_buffer_t *b) {
    freeList[freeCount++] = b;
}

void channels_init(void) {
    memset(pool, 0, sizeof(pool));
    freeCount = 0;
    for (int i = 0; i < PBX_POOL_SIZE; i++)
        give(&pool[i]);
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        channels[ch].draw = take();
        channels[ch].candidate = NULL;
    }
    rx = take();
}

static inline uint32_t pack(const uint8_t e[4]) {
    return ((uint32_t) e[0] << 24) | ((uint32_t) e[1] << 16) | ((uint32_t) e[2] << 8) | e[3];
}

void channels_begin(uint8_t type, const uint8_t *hdr) {
    build.type = type;
    build.index = 0;
    switch (type) {
    case PB_SET_CHANNEL_WS2812: {
        const pb_ws2812_channel_t *h = (const void *) hdr;
        build.pos[0] = PB_ORDER_R(h->order);
        build.pos[1] = PB_ORDER_G(h->order);
        build.pos[2] = PB_ORDER_B(h->order);
        build.pos[3] = PB_ORDER_W(h->order);
        build.cfg.kind = CH_WS2812;
        build.cfg.words = h->pixels;
        build.cfg.bitsPerWord = h->numElements * 8;
        build.cfg.frequency = PBX_WS2812_BIT_HZ;
        break;
    }
    case PB_SET_CHANNEL_APA102_DATA: {
        const pb_apa102_data_channel_t *h = (const void *) hdr;
        build.pos[0] = PB_ORDER_R(h->order) + 1;
        build.pos[1] = PB_ORDER_G(h->order) + 1;
        build.pos[2] = PB_ORDER_B(h->order) + 1;
        build.cfg.kind = CH_APA102_DATA;
        build.cfg.words = h->pixels + 2;
        build.cfg.bitsPerWord = 32;
        build.cfg.frequency = h->frequency;
        rx->data[0] = 0;  // start frame
        build.index = 1;
        break;
    }
    case PB_SET_CHANNEL_APA102_CLOCK: {
        const pb_apa102_clock_channel_t *h = (const void *) hdr;
        build.cfg.kind = CH_APA102_CLOCK;
        build.cfg.words = 0;
        build.cfg.bitsPerWord = 32;
        build.cfg.frequency = h->frequency;
        break;
    }
    case PB_SET_CHANNEL_APA102_FAST_DATA: {
        const pb_apa102_fast_data_channel_t *h = (const void *) hdr;
        build.pos[0] = PB_ORDER_R(h->order) + 1;
        build.pos[1] = PB_ORDER_G(h->order) + 1;
        build.pos[2] = PB_ORDER_B(h->order) + 1;
        build.gamma = gammaTables[h->gammaTenths - 10];
        build.cfg.kind = CH_APA102_DATA;
        build.cfg.words = h->pixels + 2;
        build.cfg.bitsPerWord = 32;
        build.cfg.frequency = h->frequency;
        rx->data[0] = 0;
        build.index = 1;
        break;
    }
    default:
        break;
    }
}

void channels_pixels(const uint8_t *wire, uint32_t n) {
    uint32_t *out = rx->data + build.index;
    switch (build.type) {
    case PB_SET_CHANNEL_WS2812: {
        uint8_t bpp = build.cfg.bitsPerWord / 8;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t e[4] = {0, 0, 0, 0};
            e[build.pos[0]] = wire[0];
            e[build.pos[1]] = wire[1];
            e[build.pos[2]] = wire[2];
            if (bpp == 4)
                e[build.pos[3]] = wire[3];
            wire += bpp;
            *out++ = pack(e);
        }
        break;
    }
    case PB_SET_CHANNEL_APA102_DATA:
        for (uint32_t i = 0; i < n; i++) {
            uint8_t e[4] = {0, 0, 0, 0};
            e[build.pos[0]] = wire[0];
            e[build.pos[1]] = wire[1];
            e[build.pos[2]] = wire[2];
            e[0] = wire[3] | 0xe0;
            wire += 4;
            *out++ = pack(e);
        }
        break;
    case PB_SET_CHANNEL_APA102_FAST_DATA:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t rgb565 = wire[0] | (wire[1] << 8);
            wire += 2;

            // 5 and 6 bit fields to 0 to 255
            uint16_t r = (((rgb565 >> 11) & 0x1f) * 269544) >> 15;
            uint16_t g = (((rgb565 >> 5) & 0x3f) * 132633) >> 15;
            uint16_t b = ((rgb565 & 0x1f) * 269544) >> 15;

            r = build.gamma[r];
            g = build.gamma[g];
            b = build.gamma[b];

            uint16_t max = r > g ? (r > b ? r : b) : (g > b ? g : b);
            uint16_t gb = (((uint32_t) max * 31) + 0xffff) >> 16;  // ceiling
            uint32_t adj = hdrAdjustTable[gb];

            uint8_t e[4] = {0, 0, 0, 0};
            e[build.pos[0]] = (r * adj) >> 16;
            e[build.pos[1]] = (g * adj) >> 16;
            e[build.pos[2]] = (b * adj) >> 16;
            e[0] = gb | 0xe0;
            *out++ = pack(e);
        }
        break;
    default:
        return;
    }
    build.index += n;
}

void channels_end(uint8_t ch, bool valid) {
    if (!valid)
        return;
    if (build.cfg.kind == CH_APA102_DATA)
        rx->data[build.index] = 0xff000000;  // end frame
    rx->cfg = build.cfg;

    channel_t *c = &channels[ch];
    if (c->candidate)
        give(c->candidate);
    c->candidate = rx;
    rx = take();
}

uint32_t *channels_receive_target(void) {
    return rx->data;
}

uint8_t *channels_update_region(uint32_t *size) {
    *size = sizeof(region.update);
    return region.update;
}

void channels_commit(void) {
    for (int ch = 0; ch < PBX_CHANNELS; ch++) {
        channel_t *c = &channels[ch];
        if (!c->candidate)
            continue;
        give(c->draw);
        c->draw = c->candidate;
        c->candidate = NULL;
    }
}
