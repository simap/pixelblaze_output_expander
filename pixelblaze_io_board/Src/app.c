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

//const static char MAGIC[] = { "UPXL" }; //starts with 0x55, good for auto baud rate detection

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

uint8_t drawingBusy;
volatile uint32_t lastDrawTimer;

volatile struct {
	uint16_t crcErrors;
	uint16_t frameMisses;
	uint16_t drawCount;
	uint16_t overDraw;
} debugStats;

static inline uint8_t getBusId() {
	uint8_t busId = 0;
	if ((GPIOB->IDR & GPIO_IDR_1)) //pb1
		busId |= 1;
	if ((GPIOA->IDR & GPIO_IDR_10)) //pb1
		busId |= 2;
	if ((GPIOF->IDR & GPIO_IDR_1)) //pb1
		busId |= 4;
	return busId;
}


static inline void startDrawingChannles() {
	if (drawingBusy || micros() - lastDrawTimer < 300) {
		debugStats.overDraw++;
		return;
	}
	debugStats.drawCount++;
	drawingBusy = 1;

	startBits = 0;
	for (int ch = 0; ch < 8; ch++) {
		if (channels[ch].numElements)
			startBits |= 1<<ch;
	}


	// pausing around here causes all white, as if tim1 was enabled and driving dma with no data bits
	DMA1_Channel3->CCR &= ~DMA_CCR_EN;
	DMA1_Channel3->CNDTR = BYTES_TOTAL;
	DMA1->IFCR |= DMA_IFCR_CTCIF3;
	DMA1_Channel3->CCR |= DMA_CCR_EN | DMA_CCR_TCIE;

	TIM1->CNT = 0; //for some reason, tim1 doesnt restart properly unless cleared.
	LL_TIM_EnableCounter(TIM1);
	LL_TIM_EnableCounter(TIM3);
	//there seems to be a pending dma request on ch3, possibly related to the timer
	//enabling dma last seems to help avoid an extra long start bit
	//there's still some slight glitches on first pixel
	//TODO see if maybe tim1 has leftover from last cycle, maybe tim3 needs adjusting
	//TODO I think the other dma channels are fighting/triggering all at the same time, maybe when tim1 is disabled


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
	LL_TIM_EnableMasterSlaveMode(TIM3);

	LL_TIM_EnableCounter(TIM1);

	LL_TIM_EnableIT_UPDATE(TIM14);
	LL_TIM_EnableCounter(TIM14);

}

static inline void handleIncomming() {
	uartResetCrc();
	if (uartGetc() == 'U' && uartGetc() == 'P' && uartGetc() == 'X' && uartGetc() == 'L') {
		volatile uint32_t timer = micros();
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

//				if (channel == 0)
//					elements[0] = elements[1] = elements[2] = 129;
//				else
//					elements[0] = elements[1] = elements[2] = 0;

//				for (int j = 0; j < ch.numElements; j++)
//					bitCopyMsb(dst + j*2, channel, elements[j]);
				bitConverter(dst, channel, elements, ch.numElements);
				dst += stride;
			}

			volatile uint32_t crcExpected = uartGetCrc();
			volatile uint32_t crcRead;
			uartRead(&crcRead, sizeof(crcRead));

			if (channel < 8) {
				int blocksToZero;
				if (crcExpected == crcRead) {
					if (ch.pixels * ch.numElements >= channels[channel].pixels * channels[channel].numElements) {
						blocksToZero = 0;
					} else {
						blocksToZero = BYTES_PER_CHANNEL - ch.numElements * ch.pixels;
					}
					channels[channel] = ch;
				} else {
					debugStats.crcErrors++;
					channels[channel].numElements = 0; //garbage, disable the channel, zero everything
					blocksToZero = BYTES_PER_CHANNEL;
				}
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + (BYTES_PER_CHANNEL - blocksToZero)*2, channel, blocksToZero);
			}
			volatile int32_t duration = micros() - timer;
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
			} else {
				debugStats.crcErrors++;
			}
			volatile long duration = microsFast() - timer;
			ms += 0;
			break;
		}
		default:
			break;
			//error
		}

	} else {
		debugStats.frameMisses++;
		toggle = !toggle;
		if (toggle) {
			GPIOF->BRR |= 1;
		} else {
			GPIOF->BSRR |= 1;
		}
	}
}

void loop() {
	for (;;) {
		//heartbeat
		if (ms - timer > 100) {
			timer = ms;

//			uint32_t t = micros();
//			for (int i = 0; i < 8; i++) {
//				bitSetZeros(bitBuffer, i, BYTES_PER_CHANNEL);
//			}
//			volatile int32_t d = micros() - t;
//			ms += 0;


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
		 //			LL_TIM_EnableCounter(TIM3);
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
