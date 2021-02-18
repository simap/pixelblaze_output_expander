#include "main.h"
#include "app.h"

uint8_t uartBuffer[UART_BUF_SIZE];
int uartPos = 0;
unsigned long uartErrors;


void uartSetup() {

	USART1->CR3 = USART_CR3_DMAR | USART_CR3_HDSEL;
	DMA1_Channel5->CMAR = (uint32_t) uartBuffer;
	DMA1_Channel5->CPAR = (uint32_t) &(USART1->RDR);
	DMA1_Channel5->CNDTR = UART_BUF_SIZE;
	DMA1_Channel5->CCR |= DMA_CCR_EN | DMA_CCR_CIRC;

	LL_USART_EnableDMAReq_RX(USART1);

	//listen for errors via interrupt
	LL_USART_EnableIT_ERROR(USART1);
}

void uartIsr() {
	//check all the uart error conditions
	if (USART1->ISR & (USART_ISR_FE | USART_ISR_ORE | USART_ISR_NE)) {
		//we don't really care to handle the error in any special way
		//the various checks and CRC should toss bad frames
		//this is more for debugging purposes and to clear the error bits
		uartErrors++;
		//for stm32l432 to clear these errors, write 1 to the appropriate bits in USART_ICR
		USART1->ICR |= USART_ICR_FECF | USART_ICR_ORECF | USART_ICR_NECF;
	}
}


void uartResetCrc() {
	//make mostly compatible with standard crc32
	CRC->CR = CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_RESET;
}


uint32_t uartGetCrc() {
	return CRC->DR ^ 0xffffffff;
}

void uartRead(void *dst, int size) {
	uint8_t *p = (uint8_t*) dst;
	while (size--)
		*p++ = uartGetc();
}

int uartAvailable() {
	int res = (UART_BUF_SIZE - DMA1_Channel5->CNDTR) - uartPos;
	if (res < 0)
		res += UART_BUF_SIZE;
	return res;
}

uint8_t uartGetc() {
	while (uartPos == (UART_BUF_SIZE - DMA1_Channel5->CNDTR)) {
		//wait
	}

	uint8_t res = uartBuffer[uartPos++];
	LL_CRC_FeedData8(CRC, res);
	if (uartPos >= UART_BUF_SIZE)
		uartPos = 0;
	return res;
}
