// Test hook. GP19 and GP20 share the data input net and GP22 is the clock
// input, and the PIO receivers read the pads regardless of who drives them.
// So a PIO transmitter on the same chip can feed either receiver:
//   mode 0: UART, 8N1 on GP20 into UART0 on GP19, at baud or the input rate
//   mode 1: SPI mode 0, data on GP20 and clock on GP22, at spiHz
// Fill loopback.data, set len, repeat, mode, and rate fields, then set
// trigger over SWD. The main loop streams the buffer repeat times without
// blocking and clears the trigger when done.
//
// extraBits (SPI only) clocks that many more bits after the last pass, to
// leave a partial byte in the receiver for the realign test.
//
// The transmitter drives the shared nets while sending. Use it only with no
// sender attached.

#ifndef LOOPBACK_H
#define LOOPBACK_H

#include <stdint.h>

// Holds one full size frame plus a draw.
#define LOOPBACK_MAX 32768

typedef struct {
    uint32_t trigger;
    uint32_t len;
    uint32_t repeat;    // passes over the buffer, 0 counts as 1
    uint32_t baud;      // UART rate, 0 for the current input rate
    uint32_t chunk;     // pause after every chunk bytes, 0 for no pauses
    uint32_t gapUs;     // length of that pause
    uint32_t rxBaud;    // if set, the UART receiver is moved to this rate at trigger
    uint32_t mode;      // 0 UART, 1 SPI
    uint32_t spiHz;     // SPI clock rate
    uint32_t extraBits; // SPI: extra clock edges after the data, 0 to 7
    uint32_t sent;      // completed passes
    uint8_t data[LOOPBACK_MAX];
} loopback_t;

extern volatile loopback_t loopback;

void loopback_init(void);
void loopback_poll(void);

#endif
