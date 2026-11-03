#include "crc32.h"

static uint32_t table[256];
static int tableReady;

static void build_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    tableReady = 1;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!tableReady)
        build_table();
    while (len--)
        crc = table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return crc;
}
