#include "main.h"
#include "app.h"
#include <string.h>

/*
 * Pixelblaze Output Expander
 *
 * This controller accepts pixel and configuration data over serial and
 * outputs WS2812 and APA102 across 8 channels.
 *
 * The basic data path looks like this:
 * uart -> dma -> circular buffer -> handleIncomming() -> bitBuffer (8 channels) -> dma + timers -> gpio
 *
 *
 * stm32l432 @ 80mhz benchmark reading byte data, doing crc, and stuffing bits
 *    crc in software: 376k bytes/sec.
 *    crc in hardware: 432k bytes/sec. over 4M baud! Could double data rate from previous expanders!
 *
 *
 * tim1 has 4 periods w/ dma triggers to set start bits, data bits, clock bits, and clear bits
 * tim1 uses all 4 channels to drive CC events to DMA for generating the output signals
 *    period: 99 (80mhz / 100 = 800khz)
 *    no prescaler
 *
 * for ws2812, timers fire in this sequence:
 *    tim1ch1 -> dma1ch2: ws2812StartBits controls the start of the pulse
 *    tim1ch3 -> dma1ch7: then data bits clear it or keep it high, ending short 0 bits
 *    tim1ch4 -> dma1ch4: then ws2812StartBits clears any channel, ending long 1 bits
 *
 * for apa102, data timers fire in this sequence:
 *    tim1ch3 -> dma1ch7: data bits set the data line. easy!
 *
 * for apa102, clock timers fire in this sequence:
 *    tim1ch3 -> dma1ch7: data bits having been zeroed, this always lowers clock line
 *    tim1ch2 -> dma1ch3: then apa102ClockBits sets channel high, causing data to latch on LEDs.
 *
 * tim15 gates tim1 and period should be long enough for data to xfer
 * prescaler matches tim1 period, then number of bytes transfered matches count so they end at the same time
 * this turns off all DMA GPIO activity after the last cycle is done
 *
 *
 * tim16 runs at 1mhz, used for a microseconds timer for WS2812 latching
 *    prescaler: 79 (80mhz / 80 = 1mhz)
 * 	  one shot mode
 * 	  set ARR and enable
 *
 * tim2 and lptim2 provide status LED PWM channels
 *
 * tim2 runs at 80mhz, used for LED PWM
 *    period: 65535 (1220.7Hz with 16 bits PWM)
 * 	  prescaler: 0
 *    tim2ch1 -> PA15 -> "Status" orange LED
 *
 * lptim2 runs at 80mhz, used for LED PWM
 *    no prescaler
 *    period 65535
 *    lptim2 -> PA8 -> "Draw" green LED
 *
 *
 *
 * The protocol starts with a magic sequence, channel, and record type
 *
 * These vars and data structures are for reference:
 * const static char MAGIC[] = { "UPXL" }; //starts with 0x55, good for auto baud rate detection
 * frame headers look like this:
 * typedef struct {
 * 	int8_t magic[4]; //"UPXL"
 * 	uint8_t channel;
 * 	uint8_t recordType; //set channel ws2812 opts+data, draw all
 * } PBFrameHeader;
 *
 * following this is a frame of the record type, optional payload data, and finished with a
 * CRC32 of the entire thing (up to and including magic)
 *
 *
 * The protocol allows specifying output data rates, however it would be hard to
 * untangle ws2812 and apa102 as they would be writing to the same port at different times,
 *
 * It would be possible to let apa102 set the speed when used without WS2812, but this is not implemented.
 * Output speed is fixed at 800KHz.
 *
 * for apa102 clock, start bits are cleared, data is zeroed out so when it copies it will toggle clock low (via tim1 data trigger)
 * tim1ch2 has a dma trigger to set clocks high (like start bits, but for clocks) half a period after data is set
 * storing a bunch of zeros is kind of a waste, but each clock channel reserves a position in the bitBuffer anyway.
 *
 * apa102 needs a start and end frame, better to write these in memory and not require it in the protocol, borrowing 2 pixels of data
 *
 *
 */

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


volatile unsigned long ms;
volatile unsigned long lastDataMs;
volatile unsigned long lastValidChannelMs;
volatile unsigned long lastDrawMs;


uint32_t bitBuffer[BYTES_PER_CHANNEL * 2];

//single byte vars for DMA to GPIO
uint8_t ones = 0xff;
uint8_t ws2812StartBits = 0;
uint8_t apa102ClockBits = 0;

volatile uint8_t drawingBusy; //set when we start drawing, cleared when dma xfer is complete


static inline void setStatusLedBrightness(uint16_t v) {
	TIM2->CCR1 = v;
}

static inline void setDrawLedBrightness(uint16_t v) {
	LPTIM2->CMP = v;
}


//some stats for debugging, in a struct to save a few bytes
volatile struct {
	uint16_t crcErrors;
	uint16_t frameMisses;
	uint16_t drawCount;
	uint16_t overDraw;
} debugStats;


//3 pins have cuttable jumpers that create an ID on a bus, bits 3-5 of the channel
static inline uint8_t getBusId() {
	uint8_t busId = 0;

	if ((GPIOB->IDR & GPIO_IDR_ID7))
		busId |= 1;
	if ((GPIOB->IDR & GPIO_IDR_ID6))
		busId |= 2;
	if ((GPIOB->IDR & GPIO_IDR_ID5))
		busId |= 4;
	return busId;
}

//given a frame's channel, check it against our busID
//return 0xff if mismatch, otherwise remapped channel (reversed)
uint8_t remapFrameChannel(uint8_t channel) {
	if (channel >> 3 != getBusId()) {
		return 0xff; //mismatch
	} else {
		return 7 - (channel & 7); //channel outputs are reversed
	}
}

//we start this one-shot timer to know when ws2812s (and ws2813s) have latched.
static inline void startWs2812LatchTimer() {
	TIM16->ARR = 300;
	TIM16->CNT = 0;
	LL_TIM_EnableCounter(TIM16);
}

static inline int isWs2812LatchTimerRunning() {
	return LL_TIM_IsEnabledCounter(TIM16);
}


static inline void startDrawingChannles() {
	if (drawingBusy) {
		debugStats.overDraw++;
		return;
	}

	//loop through all channels looking for:
	//  * the max bytes any channel wants to send and calculate TIM15 target and update dma xfer CNDTR
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


	frequency = 800000; //fixed at 800khz for now
	TIM1->ARR = 99; //80mhz / 800khz = 100
	TIM1->CCR1 = 4; //ws2812 start bits - this offset seems to produce the least jitter. gives bus time to free up
	TIM1->CCR3 = TIM1->CCR1 + 24; //triggers data + zeros clocks. 0s chop pulse to 300ns
	TIM1->CCR4 = TIM1->CCR1 + 56; //ws2812 stop bits for 1s 700ns
	TIM1->CCR2 = TIM1->CCR3 + 50; //sets clock high to latch 50% after data. trigger at same time and let DMA priority handle it.

	uint32_t maxBits = maxBytes *8;

	//check for all channels with zero data, or overflow!
	if (maxBits == 0 || maxBits > 0xffff)
		return;

	lastDrawMs = ms; //notice that we got a draw command with valid channels

	debugStats.drawCount++;
	drawingBusy = 1;

	// tim15's prescaler matches tim1's cycle so each increment of tim15 is one bit-time
	TIM15->ARR = maxBits;

	//OK this is a bit weird, there's some kind of pending DMA request that will transfer immediately and shift bits by one
	//set it up to write a zero, then set it up again
	DMA1_Channel7->CCR &= ~(DMA_CCR_EN | DMA_CCR_TCIE);
	DMA1_Channel7->CMAR = (uint32_t) &ones;
	DMA1_Channel7->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel7->CNDTR = 10;
	DMA1_Channel7->CCR |= DMA_CCR_EN;

	//turn it off, set up dma to transfer from bitBuffer
	DMA1_Channel7->CCR &= ~DMA_CCR_EN;
	DMA1_Channel7->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel7->CPAR = (uint32_t) &GPIOA->ODR;
	DMA1_Channel7->CNDTR = maxBits;
	DMA1->IFCR |= DMA_IFCR_CTCIF3;
	DMA1_Channel7->CCR |= DMA_CCR_EN | DMA_CCR_TCIE;


	TIM1->CNT = 0; //for some reason, tim1 doesnt restart properly unless cleared.
	LL_TIM_EnableCounter(TIM1);

	LL_TIM_EnableCounter(TIM15);

}

//called when the dma xfer of bitBuffer (maxBits times) is complete.
void drawingComplete() {
	drawingBusy = 0; //technically only data xfer is done, but we are still going to clear the last bit when tim1 cc3 fires
	startWs2812LatchTimer();
}

//keep track of milliseconds and update status/draw LED
void sysTickIsr() {
	ms++;
	if (ms - lastDataMs < 1000)
		setStatusLedBrightness(STATUS_LED_BRIGHTNESS);
	else
		setStatusLedBrightness((STATUS_LED_BRIGHTNESS * sinTable[(ms>>3) & 0x3f]) >> 8);

	if (ms - lastValidChannelMs < 1000 && ms - lastDrawMs < 1000)
		setDrawLedBrightness(DRAW_LED_BRIGHTNESS);
	else
		setDrawLedBrightness(0);


	//check on the SPI, see if we havent been seeing data
	spiCheckTimeout();

}

//anything not already initialized by the generated LL drivers
void setup() {
	LL_SYSTICK_EnableIT();

	//stop everything when debugging
	DBGMCU->APB1FZR1 = 0xffffffff;
	DBGMCU->APB1FZR2 = 0xffffffff;
	DBGMCU->APB2FZ = 0xffffffff;

	spiSetup();

	//set up the 4 stages of DMA triggers on tim1 channels

	//fires with TIM1_CH1 - set a start bit (if channel is enabled for ws2812)
	DMA1_Channel2->CMAR = (uint32_t) &ws2812StartBits;
	DMA1_Channel2->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel2->CNDTR = 1;
	DMA1_Channel2->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH3 - set the data bit (ws2812 data, apa102 data, apa102 clock low
	//for ws2812 this either continues the pulse or turns it into a short one
	//for apa102 data, this is the data bit
	//for apa102 clock, data is zeroed and this will lower the clock pin
	DMA1_Channel7->CMAR = (uint32_t) &bitBuffer;
	DMA1_Channel7->CPAR = (uint32_t) &GPIOA->ODR;
	//enable later

	//fires with TIM1_CH4 - clears the ws2812 pulse
	DMA1_Channel4->CMAR = (uint32_t) &ws2812StartBits;
	DMA1_Channel4->CPAR = (uint32_t) &GPIOA->BRR;
	DMA1_Channel4->CNDTR = 1;
	DMA1_Channel4->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM1_CH2 - sets the apa102 clock high
	DMA1_Channel3->CMAR = (uint32_t) &apa102ClockBits;
	DMA1_Channel3->CPAR = (uint32_t) &GPIOA->BSRR;
	DMA1_Channel3->CNDTR = 1;
	DMA1_Channel3->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//enable the dma xfers on capture compare events
	TIM1->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;

	LL_TIM_CC_SetDMAReqTrigger(TIM1, LL_TIM_CCDMAREQUEST_CC);


	//NOTE: stm cube tool does not expose this in the UI!
	LL_TIM_OC_SetMode(TIM15, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM2);
	LL_TIM_EnableMasterSlaveMode(TIM15);

	LL_TIM_EnableCounter(TIM1);


	// led pwm timers
	LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH1);
	LL_TIM_EnableCounter(TIM2);

	LL_LPTIM_Enable(LPTIM2);
	LL_LPTIM_SetWaveform(LPTIM2, LL_LPTIM_OUTPUT_WAVEFORM_PWM);
	LL_LPTIM_SetAutoReload(LPTIM2, 0xffff);
	LL_LPTIM_SetCompare(LPTIM2, 0);
	LL_LPTIM_StartCounter(LPTIM2, LL_LPTIM_OPERATING_MODE_CONTINUOUS);

}

// this is the main uart scan function. It ignores data until the magic UPXL string is seen
static inline void handleIncomming() {
	spiResetCrc();
	//look for the 4 byte magic header sequence
	if (spiGetc() == 'U' && spiGetc() == 'P' && spiGetc() == 'X' && spiGetc() == 'L') {
		uint8_t channel = spiGetc();
		uint8_t recordType = spiGetc();
		lastDataMs = ms; //notice that we see some data
		switch (recordType) {
		case SET_CHANNEL_WS2812: {
			//read in the header
			PBWS2812Channel ch;
			spiRead(&ch, sizeof(PBWS2812Channel));

			if (ch.numElements < 3 || ch.numElements > 4)
				return;
			if (ch.pixels * ch.numElements > BYTES_PER_CHANNEL)
				return;

			//check that it's addressed to us and remap
			channel = remapFrameChannel(channel);

			uint8_t or = ch.or;
			uint8_t og = ch.og;
			uint8_t ob = ch.ob;
			uint8_t ow = ch.ow;

			uint8_t elements[4];

			uint32_t * dst = bitBuffer;
			int stride = 2*ch.numElements;
			for (int i = 0; i < ch.pixels; i++) {
				elements[or] = spiGetc();
				elements[og] = spiGetc();
				elements[ob] = spiGetc();
				if (ch.numElements == 4) {
					elements[ow] = spiGetc();
				}
				//this will ignore channel > 7
				bitConverter(dst, channel, elements, ch.numElements);
				dst += stride;
			}

			volatile uint32_t crcExpected = 0, crcRead = 0;
#if USE_CRC
			crcExpected = spiGetCrc();
			spiRead((void *) &crcRead, sizeof(crcRead));
#endif
			if (channel < 8) { //check that it was addressed to us (not 0xff)
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

					lastValidChannelMs = ms;
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
			volatile uint32_t crcExpected = 0, crcRead = 0;
#if USE_CRC
			crcExpected = spiGetCrc();
			spiRead((void *) &crcRead, sizeof(crcRead));
#endif
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
			spiRead(&ch, sizeof(ch));
			if (ch.frequency == 0)
				return;
			//make sure we're not getting more data than we can handle
			if ((ch.pixels+2) * 4 > BYTES_PER_CHANNEL)
				return;

			//check that it's addressed to us and remap
			channel = remapFrameChannel(channel);

			uint8_t or = ch.or;
			uint8_t og = ch.og;
			uint8_t ob = ch.ob;

			uint8_t elements[4] = {0,0,0,0};
			uint32_t * dst = bitBuffer;

			//start frame
			bitConverter(dst, channel, elements, 4);
			dst += 8;

			for (int i = 0; i < ch.pixels; i++) {
				elements[or+1] = spiGetc();
				elements[og+1] = spiGetc();
				elements[ob+1] = spiGetc();
				elements[0] = spiGetc() | 0xe0;
				bitConverter(dst, channel, elements, 4);
				dst += 8;
			}

			//end frame
			elements[0] = 0xff;
			elements[1] = elements[2] = elements[3] = 0;
			bitConverter(dst, channel, elements, 4);

			volatile uint32_t crcExpected = 0, crcRead = 0;
#if USE_CRC
			crcExpected = spiGetCrc();
			spiRead((void *) &crcRead, sizeof(crcRead));
#endif

			if (channel < 8) { //check that it was addressed to us (not 0xff)
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

					lastValidChannelMs = ms;
				} else {
					//garbage data, disable the channel, zero everything.
					//its better to let the LEDs keep the previous values than draw garbage.
					debugStats.crcErrors++;
					channels[channel].type = SET_CHANNEL_APA102_DATA;
					memset(&channels[channel].apa102DataChannel, 0, sizeof(channels[0].apa102DataChannel));
					blocksToZero = BYTES_PER_CHANNEL;
				}
				//zero out any remaining data in the buffer for this channel
				//TODO FIXME apa102 zeros will cause a start frame or maybe draw black? might not be what we want. set to all ones instead?
				if (blocksToZero > 0)
					bitSetZeros(bitBuffer + (BYTES_PER_CHANNEL - blocksToZero)*2, channel, blocksToZero);
			}
			break;
		}
		case SET_CHANNEL_APA102_CLOCK: {
			//read in the header
			PBAPA102ClockChannel ch;
			spiRead(&ch, sizeof(ch));
			if (ch.frequency == 0)
				return;

			//check that it's addressed to us and remap
			channel = remapFrameChannel(channel);

			volatile uint32_t crcExpected = 0, crcRead = 0;
#if USE_CRC
			crcExpected = spiGetCrc();
			spiRead((void *) &crcRead, sizeof(crcRead));
#endif

			if (channel < 8) { //check that it was addressed to us (not 0xff)
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

					lastValidChannelMs = ms;
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
			//unsupported frame type or garbage frame, just wait for the next one
			break;
		}

	} else {
		debugStats.frameMisses++;
	}
}
void loop() {
	for (;;) {
		if (spiAvailable() >= 9) { //require at least 9 bytes to start processing
			handleIncomming();
		}
	}
}
