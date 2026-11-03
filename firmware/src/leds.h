#ifndef LEDS_H
#define LEDS_H

void leds_init(void);

// Call from the main loop. Updates once per millisecond.
void leds_poll(void);

#endif
