#include <string.h>
#include "imagedef.h"

#define MARKER_START 0xffffded3u
#define MARKER_END 0xab123579u

#define ITEM_IMAGE_TYPE 0x42
#define ITEM_HASH_VALUE 0x4b
#define ITEM_LAST 0xff

// A loop longer than this is treated as corrupt.
#define MAX_BLOCKS 16

// Items start with a word: type in the low byte, size in words in the next
// byte, or the next two bytes when bit 7 of the type is set.
static uint32_t item_words(uint32_t header) {
    if (header & 0x80)
        return (header >> 8) & 0xffff;
    return (header >> 8) & 0xff;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

// Parse the block at offset. Returns false if it is malformed. Stores the
// offset of the next block in the loop and, for an IMAGE_DEF, its hash.
static bool parse_block(const uint8_t *image, uint32_t len, uint32_t offset, uint32_t *next, imagedef_t *out) {
    imagedef_t d;
    memset(&d, 0, sizeof(d));
    uint32_t pos = offset + 4;
    for (;;) {
        if (pos + 4 > len)
            return false;
        uint32_t header = rd32(image + pos);
        uint32_t type = header & 0xff;
        uint32_t words = item_words(header);
        if (words == 0)
            return false;
        if (type == ITEM_LAST) {
            // Followed by the link to the next block and the end marker.
            if (pos + 12 > len || rd32(image + pos + 8) != MARKER_END)
                return false;
            *next = offset + rd32(image + pos + 4);
            if (d.found)
                *out = d;
            return true;
        }
        if (pos + words * 4 > len)
            return false;
        if (type == ITEM_IMAGE_TYPE) {
            d.found = true;
        } else if (type == ITEM_HASH_VALUE && words == 1 + IMAGEDEF_HASH_LEN / 4) {
            d.hasHash = true;
            memcpy(d.hash, image + pos + 4, IMAGEDEF_HASH_LEN);
        }
        pos += words * 4;
    }
}

void imagedef_read(const uint8_t *image, uint32_t len, imagedef_t *out) {
    memset(out, 0, sizeof(*out));
    uint32_t scan = len < IMAGEDEF_SCAN_LEN ? len : IMAGEDEF_SCAN_LEN;
    for (uint32_t first = 0; first + 4 <= scan; first += 4) {
        if (rd32(image + first) != MARKER_START)
            continue;
        uint32_t off = first;
        for (int n = 0; n < MAX_BLOCKS; n++) {
            uint32_t next;
            if (off + 4 > len || rd32(image + off) != MARKER_START)
                break;
            if (!parse_block(image, len, off, &next, out))
                break;
            if (next == first || (next & 3))
                return;
            off = next;
        }
        if (out->found)
            return;
    }
}
