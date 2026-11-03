// Per channel output buffers from a pool of 17. Each channel has a draw
// pointer pinned to its most recent committed frame, and a candidate pointer
// that may be empty. One pool buffer is the receive target. Frames stream
// into the receive target as they arrive, junk or not. A good CRC makes it
// the channel's candidate, returns the channel's old candidate to the free
// list, and takes a new receive target from the free list. A draw commits
// every candidate to its channel's draw pointer and frees the old draw
// buffers. The free list never empties: 8 draw plus 8 candidates plus one
// receive target is 17.
//
// Buffer layout is one 32 bit word per LED, first wire byte in the top byte,
// so the PIO shifts it out MSB first. WS2812 RGB uses 24 bits of each word.

#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"
#include "app.h"

// One word per pixel, sized for the smallest wire pixel, 3 byte WS2812 RGB.
#define PBX_MAX_WORDS (PB_MAX_BYTES_PER_CHANNEL / 3)

#define PBX_POOL_SIZE (2 * PBX_CHANNELS + 1)

typedef enum {
    CH_OFF = 0,
    CH_WS2812,
    CH_APA102_DATA,
    CH_APA102_CLOCK,
} ch_kind_t;

typedef struct {
    ch_kind_t kind;
    uint16_t words;        // words to send, 0 for clock channels
    uint8_t bitsPerWord;   // 24 or 32
    uint32_t frequency;    // APA102 only
} ch_config_t;

typedef struct {
    ch_config_t cfg;
    uint32_t data[PBX_MAX_WORDS];
} ch_buffer_t;

typedef struct {
    ch_buffer_t *draw;
    ch_buffer_t *candidate;   // NULL when none
} channel_t;

extern channel_t channels[PBX_CHANNELS];

void channels_init(void);

// Start a frame from its validated record header. Conversion state lives in
// the channel module, one frame at a time.
void channels_begin(uint8_t type, const uint8_t *hdr);

// Convert n pixels of wire data into the receive target.
void channels_pixels(const uint8_t *wire, uint32_t n);

// Finish the frame. valid promotes the receive target to the channel's
// candidate.
void channels_end(uint8_t ch, bool valid);

// Commit every candidate. Called at the start of a draw.
void channels_commit(void);

// The receive target's pixel words, PBX_MAX_WORDS of them. Empty between
// frames and until the next body chunk, so free for scratch use then.
uint32_t *channels_receive_target(void);

// The pool's memory as one region for an update payload. Using it destroys
// every channel buffer, so the caller stops output first and ends in a
// reboot. Returns the region and stores its size.
uint8_t *channels_update_region(uint32_t *size);

#endif
