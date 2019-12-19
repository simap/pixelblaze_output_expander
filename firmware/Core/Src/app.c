#include "main.h"
#include "app.h"
#include <string.h>

volatile unsigned long ms;
volatile unsigned long lastDataMs;

//uart -> dma -> circular buffer -> handleIncomming() -> channel buffers -> dma + timers -> gpio


//microsecond timer tim14 -> tim4 (prescaler /64)

//tim3 period should be long enough for data to xfer
//BYTES_PER_CHANNEL * 8 @ 800khz
//e.g. 2520 bpc = 20160 bits. 20160 * /800000hz = 0.0252 seconds
// at 64mhz that is 1612800 cycles
// prescaler of /28 is 57600 counts
// prescaler of /25 is 64512 counts

#define BYTES_PER_CHANNEL 2500
#define BYTES_TOTAL (BYTES_PER_CHANNEL * 8)
uint32_t bitBuffer[BYTES_PER_CHANNEL * 2]; //1920 pixels, 240x8 RGB, 180x8 RGBW


// These vars and data structures are left for reference:
//const static char MAGIC[] = { "UPXL" }; //starts with 0x55, good for auto baud rate detection
// frame headers look like this:
//typedef struct {
//	int8_t magic[4]; //"UPXL"
//	uint8_t channel;
//	uint8_t recordType; //set channel ws2812 opts+data, draw all
//} PBFrameHeader;

enum {
	SET_CHANNEL_WS2812 = 1, DRAW_ALL
} RecordType;

typedef struct {
	uint8_t numElements; //0 to disable channel, usually 3 (RGB) or 4 (RGBW)
	uint8_t or :2, og :2, ob :2, ow :2; //color orders, data on the line assumed to be RGB or RGBW
	uint16_t pixels;
} PBChannel;

PBChannel channels[8];

//single byte vars for DMA to GPIO
const uint8_t ones = 0xff;
const uint8_t zeros = 0x00;
uint8_t startBits = 0;

uint8_t drawingBusy; //set when we start drawing, cleared when dma xfer is complete
volatile uint32_t lastDrawTimer; //to allow ws2812/13 to latch, set when dma xfer is complete

static inline void ledOn() {
	GPIOC->BSRR |= GPIO_ODR_ODR15;
}
static inline void ledOff() {
	GPIOC->BRR |= GPIO_ODR_ODR15;
}

//some stats for debugging, in a struct to save a few bytes
volatile struct {
	uint16_t crcErrors;
	uint16_t frameMisses;
	uint16_t drawCount;
	uint16_t overDraw;
} debugStats;

volatile uint8_t ledBrightness;

//3 pins have cuttable jumpers that create an ID on a bus, bits 3-5 of the channel
static inline uint8_t getBusId() {
	uint8_t busId = 0;
	if ((GPIOB->IDR & GPIO_IDR_IDR0))
		busId |= 1;
	if ((GPIOB->IDR & GPIO_IDR_IDR1))
		busId |= 2;
	if ((GPIOB->IDR & GPIO_IDR_IDR2))
		busId |= 4;
	return busId;
}

static inline void startDrawingChannles() {
	//don't overdraw, and wait at least 300us to latch (use ws2813 timing)
	if (drawingBusy || micros() - lastDrawTimer < 300) {
		debugStats.overDraw++;
		return;
	}

	debugStats.drawCount++;
	drawingBusy = 1;


	startBits = 0;
	uint16_t maxBytes = 0;
	for (int ch = 0; ch < 8; ch++) {
		if (channels[ch].numElements)
			startBits |= 1<<ch;
		int chBytes = channels[ch].pixels * channels[ch].numElements;
		if (chBYtes > maxBytes)
			maxBytes = chBytes;
	}

	/*
	 * e.g. for full 2500
	 * 65535 / 2500 = 26.214
	 * round up to 27
	 *
	 *
	 * try again
	 * 2500 bytes is 20000 bits, which is 1,600,000 cycles
	 *   /65535 = 25 rounding up
	 *
	 * so we could use this to figure out a rough prescaler automatically
	 *
	 * but...
	 *
	 * since tim1 is cycling at 80 cycles, would a prescaler of 40 work? (or even 80?)
	 * so tim3's count would match bit counts.
	 *
	 *
	 * saving this working copy to git and will give it a try
	 *
	 */

	//TODO find the max bytes any channel wants to send and calculate TIM3 target and update dma xfer CNDTR
	//otherwise FPS is capped based on theoretical max bytes. was less of a problem when there was 720 bytes


	//OK this is a bit weird, there's some kind of pending DMA request that will transfer immediately and shift bits by one
	//set it up to write a zero, then set it up again
	DMA1_Channel6->CCR &= ~DMA_CCR_EN | DMA_CCR_TCIE;
	DMA1_Channel6->CMAR = (uint32_t) &ones;
	DMA1_Channel6->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel6->CNDTR = 10;
	DMA1_Channel6->CCR |= DMA_CCR_EN;

	//turn it off, set up dma to transfer from bitBuffer
	DMA1_Channel6->CCR &= ~DMA_CCR_EN;
	DMA1_Channel6->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel6->CPAR = (uint32_t) &GPIOA->ODR;
	DMA1_Channel6->CNDTR = BYTES_TOTAL;
	DMA1->IFCR |= DMA_IFCR_CTCIF3;
	DMA1_Channel6->CCR |= DMA_CCR_EN | DMA_CCR_TCIE;


	TIM1->CNT = 0; //for some reason, tim1 doesnt restart properly unless cleared.

	LL_TIM_EnableCounter(TIM1);
	LL_TIM_EnableCounter(TIM3);
}

void drawingComplete() {
	drawingBusy = 0; //technically only data xfer is done, but we are still going to clear the last bit when tim1 cc3 fires
	lastDrawTimer = micros();
//	ledOff();
}


void sysTickIsr() {
	ms++;

	if (ms - lastDataMs < 1000)
		ledBrightness = 1;
	else
		ledBrightness = 0;

	uint8_t cr = ms & 0x3;
	if (cr == 0 && ledBrightness) {
		ledOn();
	} else if (ledBrightness < cr) {
		ledOff();
	}
}


//anything not already initialized by the generated LL drivers
void setup() {
	LL_SYSTICK_EnableIT();

	//stop everything when debugging
	DBGMCU->CR = DBGMCU_CR_DBG_IWDG_STOP | DBGMCU_CR_DBG_WWDG_STOP
			| DBGMCU_CR_DBG_TIM1_STOP | DBGMCU_CR_DBG_TIM2_STOP
			| DBGMCU_CR_DBG_TIM3_STOP | DBGMCU_CR_DBG_TIM4_STOP
			| DBGMCU_CR_DBG_CAN1_STOP;


	uartSetup();

	//set up the 3 stages of DMA triggers.
	//fires with TIM1_CH1 - set a start bit (if channel is enabled)
	DMA1_Channel2->CMAR = (uint32_t) &startBits;
	DMA1_Channel2->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel2->CNDTR = 1;
	DMA1_Channel2->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH3 - set the data bit, this either continues the pulse or turns it into a short one
	DMA1_Channel6->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel6->CPAR = (uint32_t) &GPIOA->ODR;
	//enable later
//	DMA1_Channel6->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH4 - finally clears the pulse
	DMA1_Channel4->CMAR = (uint32_t) &ones;
	DMA1_Channel4->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel4->CNDTR = 1;
	DMA1_Channel4->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//enable the dma xfers on capture compare events
	TIM1->DIER = TIM_DIER_CC1DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;

	LL_TIM_EnableMasterSlaveMode(TIM3);

	LL_TIM_EnableCounter(TIM1);

	//tim4 is used for micros() timebase
	LL_TIM_EnableIT_UPDATE(TIM4);
	LL_TIM_EnableCounter(TIM4);
}

// this is the main uart scan function. It ignores data until the magic UPLX string is seen
static inline void handleIncomming() {
	uartResetCrc();
	//look for the 4 byte magic header sequence
	if (uartGetc() == 'U' && uartGetc() == 'P' && uartGetc() == 'X' && uartGetc() == 'L') {
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
				channel = 7 - (channel & 7); //channel outputs are reverse numbered
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
				bitConverter(dst, channel, elements, ch.numElements);
				dst += stride;
			}

			volatile uint32_t crcExpected = uartGetCrc();
			volatile uint32_t crcRead;
			uartRead((void *) &crcRead, sizeof(crcRead));

			if (channel < 8) {
				int blocksToZero;
				if (crcExpected == crcRead) {
					if (ch.pixels * ch.numElements >= channels[channel].pixels * channels[channel].numElements) {
						blocksToZero = 0;
					} else {
						//we need to zero out previous data if the data received was less than last time
						blocksToZero = BYTES_PER_CHANNEL - ch.numElements * ch.pixels;
					}
					channels[channel] = ch;
					lastDataMs = ms;
				} else {
					//garbage data, disable the channel, zero everything.
					//its better to let the LEDs keep the previous values than draw garbage.
					debugStats.crcErrors++;
					channels[channel].numElements = 0;
					blocksToZero = BYTES_PER_CHANNEL;
				}
				//zero out any remaining data in the buffer for this channel
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + (BYTES_PER_CHANNEL - blocksToZero)*2, channel, blocksToZero);
			}
			break;
		}
		case DRAW_ALL: {
			uint32_t crcExpected = uartGetCrc();
			uint32_t crcRead;
			uartRead(&crcRead, sizeof(crcRead));
			if (crcExpected == crcRead) {
				startDrawingChannles();
			} else {
				debugStats.crcErrors++;
			}
			break;
		}
		default:
			break;
			//unsupported op or garbage frame, just wait for the next one
		}

	} else {
		debugStats.frameMisses++;
	}
}
void loop() {
	for (;;) {
		if (uartAvailable() > 0) {
			handleIncomming();
		}
	}
}
