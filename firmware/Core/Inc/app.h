#ifndef __APP_H__
#define __APP_H__


#include <stdint.h>
#include "sintable.h"

#define UART_BUF_SIZE 8192

#define BYTES_PER_CHANNEL 4808 //1600 RGB or 1200 RGBW/HDR, a little extra for apa102 start/end frame

#define STATUS_LED_BRIGHTNESS 0x7ff
#define DRAW_LED_BRIGHTNESS 0x3ff

void setup();
void loop() ;

void bitCopyMsb(uint32_t *dst, uint8_t dstBit, uint8_t data);
void bitflip8(uint8_t * out, uint8_t * in);
void bitMangler(uint32_t * out, uint8_t in, int pos);

void bitMangler0(uint32_t * out, uint8_t in);
void bitMangler1(uint32_t * out, uint8_t in);
void bitMangler2(uint32_t * out, uint8_t in);
void bitMangler3(uint32_t * out, uint8_t in);
void bitMangler4(uint32_t * out, uint8_t in);
void bitMangler5(uint32_t * out, uint8_t in);
void bitMangler6(uint32_t * out, uint8_t in);
void bitMangler7(uint32_t * out, uint8_t in);

#ifdef __cplusplus
extern "C" {
#endif
void bitSetZeros(uint32_t *dst, uint8_t channel, int size);
void bitSetOnes(uint32_t *dst, uint8_t channel, int size);
void bitConverter(uint32_t *dst, uint8_t dstBit, uint8_t *data, int size);
#ifdef __cplusplus
}
#endif

void uartIsr();
void uartResetCrc();
uint32_t uartGetCrc();
void uartSetup();
void uartRead(void *dst, int size);
uint8_t uartGetc();
int uartAvailable();


#endif
