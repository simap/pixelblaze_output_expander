#ifndef PARSER_H
#define PARSER_H

#include "app.h"

// One parser per receive ring. A frame in progress is held here between
// polls, so switching rings mid frame needs parser_reset on the ring that
// is dropped.
typedef struct {
    ring_t *ring;
    bool inBody;
    bool mine;
    uint8_t channel;
    uint8_t type;
    uint8_t bpp;          // wire bytes per pixel
    uint32_t remaining;   // payload bytes not yet consumed
    uint32_t crc;         // running CRC from the port hook
} parser_t;

void parser_init(parser_t *p, ring_t *ring);

// Drop any frame in progress and resume hunting.
void parser_reset(parser_t *p);

// Consume whatever is available without blocking. Returns when the ring
// holds nothing the parser can act on yet.
void parser_poll(parser_t *p);

#endif
