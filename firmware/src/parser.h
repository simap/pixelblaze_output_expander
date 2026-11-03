#ifndef PARSER_H
#define PARSER_H

#include "app.h"

// Where a frame body goes while it streams out of the ring.
enum parser_body {
    BODY_SKIP = 0,    // another board's pixels, consumed without a CRC
    BODY_PIXELS,      // this board's pixels, through the bounce buffer into the channel
    BODY_CRC_ONLY,    // an update for another key, CRC checked and discarded
    BODY_UPDATE,      // an update for this board, straight into the update region
};

// One parser per receive ring. A frame in progress is held here between
// polls, so switching rings mid frame needs parser_reset on the ring that
// is dropped.
typedef struct {
    ring_t *ring;
    bool inBody;
    uint8_t body;         // enum parser_body
    uint8_t channel;
    uint8_t type;
    uint8_t bpp;          // wire bytes per pixel
    uint32_t remaining;   // payload bytes not yet consumed
    uint32_t crc;         // running CRC from the port hook
    uint8_t *payload;     // BODY_UPDATE destination
    uint32_t payloadLen;
} parser_t;

void parser_init(parser_t *p, ring_t *ring);

// Drop any frame in progress and resume hunting. An update in progress is
// aborted.
void parser_reset(parser_t *p);

// Consume whatever is available without blocking. Returns when the ring
// holds nothing the parser can act on yet.
void parser_poll(parser_t *p);

#endif
