#ifndef AUTOBAUD_H
#define AUTOBAUD_H

// Measures the UART input rate from 'U' bytes with a PIO program and moves
// the UART0 divider to match. See baud_measure in outputexpander.pio.
void autobaud_init(void);
void autobaud_poll(void);

#endif
