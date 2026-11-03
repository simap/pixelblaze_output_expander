// Reads the hash out of the IMAGE_DEF the bootrom reads, so an update
// carrying the running image is skipped instead of written. No pico-sdk
// dependency, so the host test covers it. Block format is RP2350 datasheet
// section 5.9.5.
//
// An image holds a loop of blocks linked by relative offsets. crt0 places
// the first in the first 4 KB and picotool seal appends another at the end
// of the image with the hash. The last IMAGE_DEF block in the loop is the
// one that counts.

#ifndef IMAGEDEF_H
#define IMAGEDEF_H

#include <stdint.h>
#include <stdbool.h>

// The bootrom only looks in the first 4 KB of an image for the first block.
#define IMAGEDEF_SCAN_LEN 4096

#define IMAGEDEF_HASH_LEN 32

typedef struct {
    bool found;            // at least one IMAGE_DEF block
    bool hasHash;
    uint8_t hash[IMAGEDEF_HASH_LEN];  // HASH_VALUE item, SHA-256 of the sealed image
} imagedef_t;

// Walk the block loop of an image of len bytes.
void imagedef_read(const uint8_t *image, uint32_t len, imagedef_t *out);

#endif
