#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

// Standard reflected CRC32 (IEEE 802.3, same as zlib). Pass the running
// value from a previous call to continue, starting at 0xffffffff, and the
// final value is already inverted.
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

static inline uint32_t crc32_buf(const uint8_t *data, size_t len) {
    return crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

#endif
