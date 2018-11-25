#include "main.h"
#include "app.h"

uint8_t uartBuffer[UART_BUF_SIZE];
int uartPos = 0;

void uartSetup() {
	// FU ST, putting garbage in cr3 and enabling per-clock dma requests!
	USART1->CR3 = USART_CR3_DMAR | USART_CR3_HDSEL;
	DMA1_Channel5->CMAR = (uint32_t) uartBuffer;
	DMA1_Channel5->CPAR = (uint32_t) &USART1->RDR;
	DMA1_Channel5->CNDTR = UART_BUF_SIZE;
	DMA1_Channel5->CCR |= DMA_CCR_EN;
}

void uartResetCrc() {
	//make mostly compatible with standard crc32
	CRC->CR = CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_RESET;
}

uint32_t uartGetCrc() {
	return CRC->DR ^ 0xffffffff;
}

void uartRead(void *dst, int size) {
	uint8_t *p = (uint8_t *) dst;
	while (size--)
		*p++ = uartGetc();
}

unsigned long uartErrors;

int uartAvailable() {
	int res = (UART_BUF_SIZE - DMA1_Channel5->CNDTR) - uartPos;
	if (res < 0)
		res += UART_BUF_SIZE;
	return res;
}

uint8_t uartGetc() {
	do {
		//check all the uart error conditions and clear it
		//TODO move this to an ISR, no need to check each read
		if (USART1->ISR & (USART_ISR_FE | USART_ISR_ORE | USART_ISR_NE)) {
			USART1->ICR |= USART_ICR_FECF |USART_ICR_ORECF | USART_ICR_NCF;
			uartErrors++;
		}
	} while (uartPos == (UART_BUF_SIZE - DMA1_Channel5->CNDTR));

	uint8_t res;
	LL_CRC_FeedData8(CRC, res = uartBuffer[uartPos++]);
	if (uartPos >= UART_BUF_SIZE)
		uartPos = 0;
	return res;
}
