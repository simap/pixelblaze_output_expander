#include "main.h"
#include "app.h"

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

typedef struct {
	int8_t magic[4];
	uint16_t length; //of remainder of frame
	uint8_t channel;
	uint8_t type; //or driver
	uint32_t options; //stuff like color order, speed
} PBFrameHeader;

#define UART_BUF_SIZE 128
uint8_t uartBuffer[UART_BUF_SIZE];
int uardPos = 0;

const uint8_t ones = 0xff;
const uint8_t zeros = 0;
const uint8_t test = 0;

void setup() {
	LL_SYSTICK_EnableIT();

	DBGMCU->APB1FZ = 0xffff;
	DBGMCU->APB2FZ = 0xffff;

//	DMA1_Channel3->CMAR = (uint32_t) uartBuffer;
//	DMA1_Channel3->CPAR = (uint32_t) &USART1->RDR;
//	DMA1_Channel3->CCR |= DMA_CCR_EN;


	//fires with TIM2_CH1
	DMA1_Channel5->CMAR = (uint32_t) &ones;
	DMA1_Channel5->CPAR = (uint32_t) &GPIOA->ODR;
	DMA1_Channel5->CNDTR = 1;
	DMA1_Channel5->CCR |= DMA_CCR_EN;// | DMA_CCR_TCIE;

	//fires with TIM2_CH3
	DMA1_Channel1->CMAR = (uint32_t) &ms;
	DMA1_Channel1->CPAR = (uint32_t) &GPIOA->ODR;
	DMA1_Channel1->CNDTR = 1;
	DMA1_Channel1->CCR |= DMA_CCR_EN; // | DMA_CCR_TCIE;

	//fires with TIM2_CH4
	DMA1_Channel4->CMAR = (uint32_t) &zeros;
	DMA1_Channel4->CPAR = (uint32_t) &GPIOA->ODR;
	DMA1_Channel4->CNDTR = 1;
	DMA1_Channel4->CCR |= DMA_CCR_EN;// | DMA_CCR_TCIE;

	TIM2->DIER |= TIM_DIER_CC1DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;
//			| TIM_DIER_CC1IE | TIM_DIER_CC4IE | TIM_DIER_UIE;

	LL_TIM_EnableMasterSlaveMode(TIM1);

	LL_TIM_EnableCounter(TIM2);
	LL_TIM_EnableCounter(TIM1);

}

void loop() {
	for(;;) {
//		TIM2->EGR |= TIM_EGR_CC1G;
//		TIM2->EGR |= TIM_EGR_CC4G;
		if (ms - timer > 100) {
			timer = ms;
			toggle = !toggle;
			if (toggle) {
				GPIOF->BRR |= 1;
//			GPIOA->BRR |= 0xff;
			} else {
				GPIOF->BSRR |= 1;
//			GPIOA->BSRR |= 0xff;
			}
//
			LL_TIM_DisableCounter(TIM2);
			TIM2->CNT = 0;
			LL_TIM_EnableCounter(TIM2);

			LL_TIM_EnableCounter(TIM1);

		}
	}
}
