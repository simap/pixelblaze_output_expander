#include "main.h"
#include "app.h"
#include <string.h>

volatile unsigned long ms;
unsigned long timer;

int toggle = 0;

//uart -> dma -> command buffer -> handleCommand() -> pixel buffers -> dma + timers -> gpio

//frame, use OPC?
//channel	command	length (n)	data
//0 to 255	0 to 255	high byte	low byte	n bytes of message data
//can use our chip address to offset by multiples of 8 for channel

//"pixl", channel, type, length[16], options[32],, data[], CRC

// on timer update : trigger writing ones
// on timer pwm 1: write from data
// on timer pwm 2: write zeros (or write ones to reset register)

//somehow stop this after all data is sent - maybe a timer, maybe dma xfer complete interrrupt
//i might be OK for a few stray '1' bits to go out?
//when dma is done, pwm 1 will stop clearing the bits, causing a stream of '1's to be written
//maybe some way to slave the timer?

#define BYTES_PER_CHANNEL 720
#define BYTES_TOTAL (BYTES_PER_CHANNEL * 8)
uint32_t bitBuffer[BYTES_PER_CHANNEL * 2]; //1920 pixels, 240x8 RGB, 180x8 RGBW

//TODO short timeout on rx and reset auto baud rate detection

const static char MAGIC[] = { "UPXL" }; //starts with 0x55, good for auto baud rate detection

typedef struct {
	int8_t magic[4];
	uint8_t channel;
	uint8_t recordType; //set channel ws2812 opts+data, draw all
} PBFrameHeader;

enum {
	SET_CHANNEL_WS2812 = 1, DRAW_ALL, BLINK_ID_LED
} RecordType;

typedef struct {
	uint8_t numElements; //0 to disable channel, usually 3 or 4
	uint8_t or :2, og :2, ob :2, ow :2; //color orders, data on the line assumed to be RGB or RGBW
	uint16_t pixels;
} PBChannel;

PBChannel channels[8];


const uint8_t ones = 0xff;
uint8_t startBits = 0;

volatile uint8_t drawingBusy;

uint8_t getBusId() {
	uint8_t busId = 0;
	if ((GPIOB->IDR & GPIO_IDR_1)) //pb1
		busId |= 1;
	if ((GPIOA->IDR & GPIO_IDR_10)) //pb1
		busId |= 2;
	if ((GPIOF->IDR & GPIO_IDR_1)) //pb1
		busId |= 4;
	return busId;
}

unsigned long drawCount = 0;

void startDrawingChannles() {
	if (drawingBusy) {
		return;
	}

	drawCount++;
	drawingBusy = 1;
	LL_TIM_DisableCounter(TIM1);
	TIM1->CNT = 0;
	LL_TIM_EnableCounter(TIM1);

	DMA1_Channel3->CCR &= ~DMA_CCR_EN;
	DMA1_Channel3->CNDTR = BYTES_TOTAL;
	DMA1_Channel3->CCR |= DMA_CCR_EN | DMA_CCR_TCIE;


	DMA1->IFCR |= DMA_IFCR_CTCIF3;

	startBits = 0;
	for (int ch = 0; ch < 8; ch++) {
		if (channels[ch].numElements)
			startBits |= 1<<ch;
	}

	LL_TIM_EnableCounter(TIM2);
}

void setup() {
	LL_SYSTICK_EnableIT();

	DBGMCU->APB1FZ = 0xffff;
	DBGMCU->APB2FZ = 0xffff;

	uartSetup();

	//fires with TIM1_CH1
	DMA1_Channel2->CMAR = (uint32_t) &startBits;
	DMA1_Channel2->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel2->CNDTR = 1;
	DMA1_Channel2->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH2
	DMA1_Channel3->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel3->CPAR = (uint32_t) &GPIOA->ODR;
	//enable later
//	DMA1_Channel3->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH4
	DMA1_Channel4->CMAR = (uint32_t) &ones;
	DMA1_Channel4->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel4->CNDTR = 1;
	DMA1_Channel4->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	TIM1->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC4DE;

//	LL_TIM_EnableMasterSlaveMode(TIM1);
	LL_TIM_EnableMasterSlaveMode(TIM2);

	LL_TIM_EnableCounter(TIM1);

}

void handleIncomming() {
	uartResetCrc();
	if (uartGetc() == 'U' && uartGetc() == 'P' && uartGetc() == 'X' && uartGetc() == 'L') {
		unsigned long timer = ms;
		uint8_t channel = uartGetc();
		uint8_t recordType = uartGetc();
		switch (recordType) {
		case SET_CHANNEL_WS2812: {

			//read in the header
			PBChannel ch;
			uartRead(&ch, sizeof(ch));
			if (ch.numElements < 3 || ch.numElements > 4)
				return;
			if (ch.pixels * ch.numElements > BYTES_PER_CHANNEL)
				return;

			//check that it's one of ours
			//TODO handle broadcast?
			if (channel >> 3 != getBusId()) {
				//follow along, but ignore data
				channel = 0xff;
			} else {
				channel = channel & 7;
			}

			uint8_t or = ch.or;
			uint8_t og = ch.og;
			uint8_t ob = ch.ob;
			uint8_t ow = ch.ow;

			uint8_t elements[4];

			uint32_t * dst = bitBuffer;
			int stride = 2*ch.numElements;
			for (int i = 0; i < ch.pixels; i++) {
				elements[or] = uartGetc();
				elements[og] = uartGetc();
				elements[ob] = uartGetc();
				if (ch.numElements == 4) {
					elements[ow] = uartGetc();
				}
//				for (int j = 0; j < ch.numElements; j++)
//					bitCopyMsb(dst + j*2, channel, elements[j]);
				bitConverter(dst, channel, elements, ch.numElements);
				dst += stride;
			}

			uint32_t crcExpected = uartGetCrc();
			uint32_t crcRead;
			uartRead(&crcRead, sizeof(crcRead));

			if (channel < 8) {
				int zeroStart = 0;
				if (crcExpected == crcRead) {
					channels[channel] = ch;
					zeroStart = ch.numElements * ch.pixels * 2;
				} else {
					channels[channel].numElements = 0; //garbage, disable the channel, zero everything
				}

				//zero remaining data
				int blocksToZero = BYTES_PER_CHANNEL - zeroStart/2;
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + zeroStart, channel, blocksToZero);
			}
			volatile unsigned long duration = ms - timer;
			ms += 0;
			break;
		}
		case DRAW_ALL: {
			//TODO check to make sure its not still drawing
			uint32_t crcExpected = uartGetCrc();
			uint32_t crcRead;
			uartRead(&crcRead, sizeof(crcRead));
			if (crcExpected == crcRead) {
				startDrawingChannles();
			}
			break;
		}
		default:
			break;
			//error
		}
	}
}

void loop() {
	for (;;) {
		//heartbeat
		if (ms - timer > 100) {
			timer = ms;
			toggle = !toggle;
			if (toggle) {
				GPIOF->BRR |= 1;
			} else {
				GPIOF->BSRR |= 1;
			}
		}

		if (uartAvailable() > 0) {
			handleIncomming();
		}

		/*
		 unsigned long t1 = ms;
		 uint8_t in[8] = {1,2,3,4,5,6,7,8};
		 uint8_t out[8];
		 for (int i = 0; i < 100000; i++) {
		 //			bitflip8(out, in);
		 bitMangler0(out, i);
		 //			LL_TIM_EnableCounter(TIM2);
		 }

		 volatile unsigned long t2 = ms - t1;

		 //100k 8 byte flips in 1543ms = 518KB/s
		 //8x 800Kbps = 800KBps
		 //to slow to flip in real time

		 if (out[0] == 0xff && t2 > 100) {
		 GPIOF->BSRR |= 1;
		 }

		 */
	}

}
