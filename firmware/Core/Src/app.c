#include "main.h"
#include "app.h"
#include <string.h>

volatile unsigned long ms;
volatile unsigned long lastDataMs;



//with uart dma, 12 cycles of jitter = 188ns
//with uart dma and cpu w/ nops, 17 cycles. = 265ns
//with uart dma and cpu w/o nops, 20 cycles. = 312ns


//uart -> dma -> circular buffer -> handleIncomming() -> bitBuffer (8 channels) -> dma + timers -> gpio

//microsecond timer on tim4 (prescaler /64)

//tim1 has 3 periods w/ dma triggers to set start bits, data bits from buffer, and clear bits
//tim3 gates tim1 and period should be long enough for data to xfer
//tim3 prescaler matches tim1 period, then number of bytes transfered matches count so they end at the same time

//it would be hard to untangle ws2812 and apa102 as they would be writing to the same port at different times,
//so if both are enabled speed is set at 800khz.
//otherwise, apa102 can set the speed. I want the protocol to support per-channel settings, but the board
//will be limited to one speed. last speed wins? lowest/safest speed?

//for apa102 clock, start bits are cleared, data is zeroed out so when it copies it will toggle clock low (via tim1 data trigger)
//tim2 has a dma trigger to set clocks high (like start bits, but for clocks) half a period (40 cycles) after data is set
//each clock channel needed a bit position anyway.

//apa102 needs a start and end frame, better to write these in memory and not require it in the protocol, borrowing 2 pixels of data


#define BYTES_PER_CHANNEL 2408 //800 RGB or 600 RGBW/HDR, a little extra for apa102 start/end frame
#define BYTES_TOTAL (BYTES_PER_CHANNEL * 8)
uint32_t bitBuffer[BYTES_PER_CHANNEL * 2];


// These vars and data structures are left for reference:
//const static char MAGIC[] = { "UPXL" }; //starts with 0x55, good for auto baud rate detection
// frame headers look like this:
//typedef struct {
//	int8_t magic[4]; //"UPXL"
//	uint8_t channel;
//	uint8_t recordType; //set channel ws2812 opts+data, draw all
//} PBFrameHeader;

enum RecordType {
	SET_CHANNEL_WS2812 = 1, DRAW_ALL, SET_CHANNEL_APA102_DATA, SET_CHANNEL_APA102_CLOCK
};

typedef struct {
	uint8_t numElements; //0 to disable channel, usually 3 (RGB) or 4 (RGBW)
	uint8_t or :2, og :2, ob :2, ow :2; //color orders, data on the line assumed to be RGB or RGBW
	uint16_t pixels;
} PBWS2812Channel;

typedef struct {
	uint32_t frequency;
	uint8_t or :2, og :2, ob :2; //color orders, data on the line assumed to be RGBV (global brightness last)
	uint16_t pixels;
} PBAPA102DataChannel;

typedef struct {
	uint32_t frequency;
} PBAPA102ClockChannel;


typedef struct {
	enum RecordType type;
	union {
		PBWS2812Channel ws2812Channel;
		PBAPA102DataChannel apa102DataChannel;
		PBAPA102ClockChannel apa102ClockChannel;
	};
} PBChannel;

PBChannel channels[8];

//single byte vars for DMA to GPIO
uint8_t ones = 0xff;
//const uint8_t zeros = 0x00;
uint8_t ws2812StartBits = 0;
uint8_t apa102ClockBits = 0;

volatile uint8_t drawingBusy; //set when we start drawing, cleared when dma xfer is complete
//volatile uint32_t lastDrawTimer; //to allow ws2812/13 to latch, set when dma xfer is complete

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

//volatile uint8_t ledBrightness;

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

static inline void startWs2812LatchTimer() {
	TIM4->ARR = 300;
	LL_TIM_EnableCounter(TIM4);
}

static inline int isWs2812LatchTimerRunning() {
	return LL_TIM_IsEnabledCounter(TIM4);
}


static inline void startDrawingChannles() {
	if (drawingBusy) {
		debugStats.overDraw++;
		return;
	}

	//loop through all channels looking for:
	//  * the max bytes any channel wants to send and calculate TIM3 target and update dma xfer CNDTR
	//    otherwise FPS is capped based on theoretical max bytes. was less of a problem when there was 720 bytes
	//  * if any channel is ws2812 and latch time isn't done, then stop now
	//  * set the ws2812StartBits for enabled ws2812 channels
	//  * set the apa102ClockBits for enabled apa102 channels
	//  * find the lowest apa102 frequency
	ws2812StartBits = 0;
	apa102ClockBits = 0;
	int maxBytes = 0;
	int frequency = -1;
	for (int ch = 0; ch < 8; ch++) {
		switch (channels[ch].type) {
		case SET_CHANNEL_WS2812:
			if (channels[ch].ws2812Channel.numElements) {
				if (isWs2812LatchTimerRunning()) {
					debugStats.overDraw++;
					return;
				}
				//don't send start bits for disabled channels
				ws2812StartBits |= 1<<ch;
			}
			int chBytes = channels[ch].ws2812Channel.pixels * channels[ch].ws2812Channel.numElements;
			if (chBytes > maxBytes)
				maxBytes = chBytes;
			break;
		case SET_CHANNEL_APA102_DATA:
			if (channels[ch].apa102DataChannel.frequency) {
				int chBytes = channels[ch].apa102DataChannel.pixels * 4;
				if (chBytes > maxBytes)
					maxBytes = chBytes;
				if (frequency == -1 || channels[ch].apa102DataChannel.frequency < frequency)
					frequency = channels[ch].apa102DataChannel.frequency;
			}
			break;
		case SET_CHANNEL_APA102_CLOCK:
			if (channels[ch].apa102ClockChannel.frequency) {
				apa102ClockBits |= 1<<ch;
				if (frequency == -1 || channels[ch].apa102ClockChannel.frequency < frequency)
					frequency = channels[ch].apa102ClockChannel.frequency;
			}
			break;
		}
	}

//	frequency = 2000000;
//
//	//ws2812 clock overrides anything else
//	if (ws2812StartBits || frequency <= 0) {
		frequency = 800000;
		TIM1->ARR = TIM2->ARR = 79; //64mhz / 800khz = 80
		TIM1->CCR1 = 1; //ws2812 start bits
		TIM1->CCR3 = 16; //triggers data + zeros clocks
		TIM1->CCR4 = 56; //ws2812 stop bits
		TIM2->CCR2 = 57; //sets clock high to latch
//	} else {
//		int reload = (SystemCoreClock / frequency) - 1;
//		if (reload < 4)
//			reload = 4;
//		TIM1->ARR = TIM2->ARR = reload;
//		TIM1->CCR1 = 2; //does this fire?
//		TIM1->CCR3 = 1; //triggers data + zeros clocks
//		TIM1->CCR4 = 3; //does this fire?
//		TIM2->CCR2 = TIM1->ARR>>1; //sets clock high to latch
//	}

	int maxBits = maxBytes *8;

	//all channels have length of zero!
	if (maxBits == 0)
		return;

	debugStats.drawCount++;
	drawingBusy = 1;

	// tim3's prescaler matches tim1's cycle so each increment of tim3 is one bit-time
	TIM3->ARR = maxBits;

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
	DMA1_Channel6->CNDTR = maxBits;
	DMA1->IFCR |= DMA_IFCR_CTCIF3;
	DMA1_Channel6->CCR |= DMA_CCR_EN | DMA_CCR_TCIE;


	TIM1->CNT = 0; //for some reason, tim1 doesnt restart properly unless cleared.
	TIM2->CNT = 0;

	LL_TIM_EnableCounter(TIM1);
	LL_TIM_EnableCounter(TIM3);

//	__WFI();

}

void drawingComplete() {
	drawingBusy = 0; //technically only data xfer is done, but we are still going to clear the last bit when tim1 cc3 fires
	startWs2812LatchTimer();
//	ledOff();
}


//void sysTickIsr() {
//	ms++;
//
//	if (ms - lastDataMs < 1000)
//		ledBrightness = 1;
//	else
//		ledBrightness = 0;
//
//	uint8_t cr = ms & 0x3;
//	if (cr == 0 && ledBrightness) {
//		ledOn();
//	} else if (ledBrightness < cr) {
//		ledOff();
//	}
//}

//anything not already initialized by the generated LL drivers
void setup() {
//	LL_SYSTICK_EnableIT();

	//stop everything when debugging
	DBGMCU->CR = DBGMCU_CR_DBG_IWDG_STOP | DBGMCU_CR_DBG_WWDG_STOP
			| DBGMCU_CR_DBG_TIM1_STOP | DBGMCU_CR_DBG_TIM2_STOP
			| DBGMCU_CR_DBG_TIM3_STOP | DBGMCU_CR_DBG_TIM4_STOP
			| DBGMCU_CR_DBG_CAN1_STOP;


	uartSetup();

	//set up the 4 stages of DMA triggers, spread across tim1 and tim2

	//fires with TIM1_CH1 - set a start bit (if channel is enabled for ws2812)
	DMA1_Channel2->CMAR = (uint32_t) &ws2812StartBits;
	DMA1_Channel2->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel2->CNDTR = 1;
	DMA1_Channel2->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH3 - set the data bit (ws2812 data, apa102 data, apa102 clock low
	//for ws2812 this either continues the pulse or turns it into a short one
	//for apa102 data, this is the data bit
	//for apa102 clock, data is zeroed and this will lower the clock pin
	DMA1_Channel6->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel6->CPAR = (uint32_t) &GPIOA->ODR;
	//enable later
//	DMA1_Channel6->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH4 - clears the ws2812 pulse
	DMA1_Channel4->CMAR = (uint32_t) &ws2812StartBits;
	DMA1_Channel4->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel4->CNDTR = 1;
	DMA1_Channel4->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM2_CH2 - sets the apa102 clock high
	DMA1_Channel7->CMAR = (uint32_t) &apa102ClockBits;
	DMA1_Channel7->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel7->CNDTR = 1;
	DMA1_Channel7->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//enable the dma xfers on capture compare events
	TIM1->DIER = TIM_DIER_CC1DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;
	TIM2->DIER = TIM_DIER_CC2DE;

	LL_TIM_EnableMasterSlaveMode(TIM3);

	LL_TIM_EnableCounter(TIM1);
	LL_TIM_EnableCounter(TIM2);

}

// this is the main uart scan function. It ignores data until the magic UPXL string is seen
static inline void handleIncomming() {
	uartResetCrc();
	//look for the 4 byte magic header sequence
	if (uartGetc() == 'U' && uartGetc() == 'P' && uartGetc() == 'X' && uartGetc() == 'L') {
		uint8_t channel = uartGetc();
		uint8_t recordType = uartGetc();
		switch (recordType) {
		case SET_CHANNEL_WS2812: {
			//read in the header
			PBWS2812Channel ch;
			uartRead(&ch, sizeof(PBWS2812Channel));

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
				ledOn();
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
				//this will ignore channel > 7
				bitConverter(dst, channel, elements, ch.numElements);
				dst += stride;
			}

			volatile uint32_t crcExpected = uartGetCrc();
			volatile uint32_t crcRead;
			uartRead((void *) &crcRead, sizeof(crcRead));

			ledOff();
			if (channel < 8) {
				int blocksToZero;
				if (crcExpected == crcRead) {
					if (channels[channel].type == SET_CHANNEL_WS2812
							&& (ch.pixels * ch.numElements >=
									channels[channel].ws2812Channel.pixels * channels[channel].ws2812Channel.numElements)
						) {
						blocksToZero = 0;
					} else {
						//we need to zero out previous data if the data received was less than last time
						blocksToZero = BYTES_PER_CHANNEL - ch.numElements * ch.pixels;
					}

					channels[channel].type = SET_CHANNEL_WS2812;
					channels[channel].ws2812Channel = ch;

					lastDataMs = ms;
				} else {
					//garbage data, disable the channel, zero everything.
					//its better to let the LEDs keep the previous values than draw garbage.
					debugStats.crcErrors++;
					channels[channel].type = SET_CHANNEL_WS2812;
					memset(&channels[channel].ws2812Channel, 0, sizeof(channels[0].ws2812Channel));
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
		case SET_CHANNEL_APA102_DATA: {
			//read in the header
			PBAPA102DataChannel ch;
			uartRead(&ch, sizeof(ch));
			if (ch.frequency == 0)
				return;
			//make sure we're not getting more data than we can handle
			if ((ch.pixels+2) * 4 > BYTES_PER_CHANNEL)
				return;

			//check that it's one of ours
			//TODO handle broadcast?
			if (channel >> 3 != getBusId()) {
				//follow along, but ignore data
				channel = 0xff;
			} else {
				channel = 7 - (channel & 7); //channel outputs are reverse numbered
				ledOn();
			}

			uint8_t or = ch.or;
			uint8_t og = ch.og;
			uint8_t ob = ch.ob;

			uint8_t elements[4] = {0,0,0,0};
			uint32_t * dst = bitBuffer;

			//start frame
			bitConverter(dst, channel, elements, 4);
			dst += 8;

			for (int i = 0; i < ch.pixels; i++) {
				elements[or+1] = uartGetc();
				elements[og+1] = uartGetc();
				elements[ob+1] = uartGetc();
				elements[0] = uartGetc() | 0xe0;
				bitConverter(dst, channel, elements, 4);
				dst += 8;
			}

			//end frame
			elements[0] = 0xff;
			elements[1] = elements[2] = elements[3] = 0;
			bitConverter(dst, channel, elements, 4);

			volatile uint32_t crcExpected = uartGetCrc();
			volatile uint32_t crcRead;
			uartRead((void *) &crcRead, sizeof(crcRead));

			ledOff();
			if (channel < 8) {
				int blocksToZero;
				if (crcExpected == crcRead) {
					if (channels[channel].type == SET_CHANNEL_APA102_DATA
							&& (ch.pixels >= channels[channel].apa102DataChannel.pixels)
						) {
						blocksToZero = 0;
					} else {
						//we need to zero out previous data if the data received was less than last time
						blocksToZero = BYTES_PER_CHANNEL - ch.pixels * 4;
					}

					channels[channel].type = SET_CHANNEL_APA102_DATA;
					channels[channel].apa102DataChannel = ch;

					lastDataMs = ms;
				} else {
					//garbage data, disable the channel, zero everything.
					//its better to let the LEDs keep the previous values than draw garbage.
					debugStats.crcErrors++;
					channels[channel].type = SET_CHANNEL_APA102_DATA;
					memset(&channels[channel].apa102DataChannel, 0, sizeof(channels[0].apa102DataChannel));
					blocksToZero = BYTES_PER_CHANNEL;
				}
				//zero out any remaining data in the buffer for this channel
				//TODO FIXME apa102 zeros will cause a start frame, not what we want. set to all ones instead
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + (BYTES_PER_CHANNEL - blocksToZero)*2, channel, blocksToZero);
			}
			break;
		}
		case SET_CHANNEL_APA102_CLOCK: {
			//read in the header
			PBAPA102ClockChannel ch;
			uartRead(&ch, sizeof(ch));
			if (ch.frequency == 0)
				return;

			//check that it's one of ours
			//TODO handle broadcast?
			if (channel >> 3 != getBusId()) {
				//follow along, but ignore data
				channel = 0xff;
			} else {
				channel = 7 - (channel & 7); //channel outputs are reverse numbered
				ledOn();
			}

			volatile uint32_t crcExpected = uartGetCrc();
			volatile uint32_t crcRead;
			uartRead((void *) &crcRead, sizeof(crcRead));

			ledOff();
			if (channel < 8) {
				int blocksToZero;
				if (crcExpected == crcRead) {
					if (channels[channel].type == SET_CHANNEL_APA102_CLOCK) {
						blocksToZero = 0;
					} else {
						//we need to zero out previous data
						blocksToZero = BYTES_PER_CHANNEL;
					}

					channels[channel].type = SET_CHANNEL_APA102_CLOCK;
					channels[channel].apa102ClockChannel = ch;

					lastDataMs = ms;
				} else {
					//garbage data, disable the channel, zero everything. Some apa102 channels could be without clock, so should remain unchanged
					debugStats.crcErrors++;
					channels[channel].type = SET_CHANNEL_APA102_CLOCK;
					memset(&channels[channel].apa102ClockChannel, 0, sizeof(channels[0].apa102ClockChannel));
					blocksToZero = BYTES_PER_CHANNEL;
				}
				//zero out any remaining data in the buffer for this channel
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + (BYTES_PER_CHANNEL - blocksToZero)*2, channel, blocksToZero);
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
