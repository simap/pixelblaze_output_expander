#include "main.h"
#include "app.h"

uint8_t uartBuffer[UART_BUF_SIZE];
int uartPos = 0;
unsigned long uartErrors;


static void uartStartTimeoutDetection() {
	//sanity check BRR ~19.2 (2.5Mbps) - 416.6 (115.2Kbps)
	if (USART1->BRR < 15 || USART1->BRR > 600)
		USART1->BRR = 48;
	//TODO calc TOR, shoot for 250us
	USART1->RTOR = 48000000 / (USART1->BRR * 4000);

	SET_BIT(USART1->CR1, USART_CR1_RTOIE);
	SET_BIT(USART1->CR2, USART_CR2_RTOEN);

	//stop listening for RXNE interrupt
	CLEAR_BIT(USART1->CR1, USART_CR1_RXNEIE);
}


static void uartStartBaudRateDetection() {
	//disable timeout interrupt
//	CLEAR_BIT(USART1->CR1, USART_CR1_RTOIE);
//	CLEAR_BIT(USART1->CR2, USART_CR2_RTOEN);

	// "It is cleared by software, in order to request a new auto baud rate detection, by writing 1 to the ABRRQ in the USART_RQR register."
	SET_BIT(USART1->RQR, USART_RQR_ABRRQ);
	//enable RXNE interrupt
	SET_BIT(USART1->CR1, USART_CR1_RXNEIE); //DMA will clear RXNE, but we can look for ABRF

	//apparently not this
	// "At any later time, the auto baud rate detection may be relaunched by resetting the ABRF flag (by writing a 0)."
	//	CLEAR_BIT(USART1->ISR, USART_ISR_ABRF);
}

void uartSetup() {
	// FU ST, putting garbage in cr3 and enabling per-clock dma requests!
	USART1->CR3 = USART_CR3_DMAR | USART_CR3_HDSEL;
	DMA1_Channel5->CMAR = (uint32_t) uartBuffer;
	DMA1_Channel5->CPAR = (uint32_t) &USART1->RDR;
	DMA1_Channel5->CNDTR = UART_BUF_SIZE;
	DMA1_Channel5->CCR |= DMA_CCR_EN;

	//listen for errors via interrupt
	SET_BIT(USART1->CR3, USART_CR3_EIE);

//	uartStartBaudRateDetection();
}

void uartIsr() {
	/*
	//when rx times out, restart auto baud rate detection (and stop looking for timeouts)
	if (USART1->ISR & USART_ISR_RTOF) {
		USART1->ICR |= USART_ICR_RTOCF;
//		LL_USART_SetAutoBaudRateMode(USART1, LL_USART_AUTOBAUD_DETECT_ON_55_FRAME);
		uartStartBaudRateDetection();
	}

	// check for RXNE interrupt during baud rate detection. DMA will have cleared RXNE
	if ((USART1->CR1 & USART_CR1_RXNEIE) && (USART1->ISR & USART_ISR_ABRF)) {
		if (USART1->ISR & USART_ISR_ABRE) {
			//baud rate detection error, should we restart immediately, or wait for rx timeout?
			// maybe make a guess for baud rate so that timeouts are reasonable
			USART1->BRR = 48; //default to 1mbps
		}
		uartStartTimeoutDetection();
	}
*/
	//check all the uart error conditions and clear it
	if (USART1->ISR & (USART_ISR_FE | USART_ISR_ORE | USART_ISR_NE)) {
//		USART1->ICR |= USART_ICR_FECF |USART_ICR_ORECF | USART_ICR_NCF;
		uartErrors++;
	}

	USART1->ICR = USART1->ISR;
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


int uartAvailable() {
	int res = (UART_BUF_SIZE - DMA1_Channel5->CNDTR) - uartPos;
	if (res < 0)
		res += UART_BUF_SIZE;
	return res;
}

uint8_t uartGetc() {
//	unsigned long t = 0;
	do {
//		if (t++ == 6000) {
//			uartStartBaudRateDetection();
//		}
	} while (uartPos == (UART_BUF_SIZE - DMA1_Channel5->CNDTR));

	uint8_t res;
	LL_CRC_FeedData8(CRC, res = uartBuffer[uartPos++]);
	if (uartPos >= UART_BUF_SIZE)
		uartPos = 0;
	return res;
}
