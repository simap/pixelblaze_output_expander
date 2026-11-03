// Wire format shared with Pixelblaze and the v1 to v3 expanders.
// All multi byte fields are little endian. Structs are copied byte for byte
// off the wire, so each one is packed and includes the padding the original
// unpacked C structs had on the STM32.

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PB_MAGIC_0 'U'
#define PB_MAGIC_1 'P'
#define PB_MAGIC_2 'X'
#define PB_MAGIC_3 'L'

#define PB_FRAME_HEADER_LEN 6
#define PB_CRC_LEN 4

enum pb_record_type {
    PB_SET_CHANNEL_WS2812 = 1,
    PB_DRAW_ALL = 2,
    PB_SET_CHANNEL_APA102_DATA = 3,
    PB_SET_CHANNEL_APA102_CLOCK = 4,
    PB_SET_CHANNEL_APA102_FAST_DATA = 5,
    PB_UPDATE = 6,
};

// Colour order byte. Two bits per element give the output position of each
// wire element. Wire data is RGB or RGBW.
#define PB_ORDER_R(o) ((o) & 3)
#define PB_ORDER_G(o) (((o) >> 2) & 3)
#define PB_ORDER_B(o) (((o) >> 4) & 3)
#define PB_ORDER_W(o) (((o) >> 6) & 3)

typedef struct __attribute__((packed)) {
    uint8_t numElements;  // 3 RGB or 4 RGBW. Other values reject the frame.
    uint8_t order;
    uint16_t pixels;
} pb_ws2812_channel_t;

typedef struct __attribute__((packed)) {
    uint32_t frequency;   // Hz. 0 rejects the frame.
    uint8_t order;        // wire data is RGBV, brightness last
    uint8_t pad;
    uint16_t pixels;
} pb_apa102_data_channel_t;

typedef struct __attribute__((packed)) {
    uint32_t frequency;
} pb_apa102_clock_channel_t;

typedef struct __attribute__((packed)) {
    uint32_t frequency;
    uint8_t order;
    uint8_t gammaTenths;  // 10 to 25
    uint16_t pixels;      // wire data is RGB565, 2 bytes per pixel
} pb_apa102_fast_data_channel_t;

// Payload size limit per channel. 4000 APA102 pixels plus start and end
// frames, or 5336 RGB WS2812 pixels. The v3 expander allowed 4808.
#define PB_MAX_BYTES_PER_CHANNEL 16008

// Update record. Broadcast, the channel byte is ignored. The key names the
// hardware and payload type, for example "RP2350_ARM-2MB-standard-firmware",
// NUL padded and compared up to the first NUL. A board accepts only the keys
// it was built with. crc is the CRC32 of key and length, checked before the
// parser commits to a body it cannot rewind. The payload is a sealed
// firmware image as flashed, and the frame CRC follows it as usual.
#define PB_UPDATE_KEY_LEN 64
#define PB_UPDATE_MAX_LEN (8u << 20)

typedef struct __attribute__((packed)) {
    char key[PB_UPDATE_KEY_LEN];
    uint32_t length;
    uint32_t crc;
} pb_update_t;

#define PB_MAX_RECORD_HEADER_LEN sizeof(pb_update_t)
#define PB_MAX_FRAME_LEN (PB_FRAME_HEADER_LEN + PB_MAX_RECORD_HEADER_LEN + PB_MAX_BYTES_PER_CHANNEL + PB_CRC_LEN)

#endif
