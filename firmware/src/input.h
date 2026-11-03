#ifndef INPUT_H
#define INPUT_H

// Starts the UART and SPI receivers, their DMA rings, and the CRC sniffer.
void input_init(void);

// Refreshes ring write pointers, picks the live input, and runs the parser.
void input_poll(void);

#endif
