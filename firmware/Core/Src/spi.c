#include "main.h"
#include "app.h"

uint8_t spiBuffer[SPI_BUF_SIZE];
int spiPos = 0;
unsigned long spiErrors;


volatile uint8_t isSpiFresh;
uint32_t spiIdleCount;
uint32_t lastDmaCndtr;

extern volatile unsigned long ms;


void spiCheckTimeout() {
	if (lastDmaCndtr != DMA2_Channel3->CNDTR) {
		lastDmaCndtr = DMA2_Channel3->CNDTR;
		spiIdleCount = 0;
		isSpiFresh = 0;
	} else if (!isSpiFresh){
		if (++spiIdleCount >= SPI_TIMEOUT_MS) {

			//reset SPI state

			SET_BIT(SPI1->CR1, SPI_CR1_SSI);
			LL_SPI_Disable(SPI1);
			LL_SPI_Enable(SPI1);
			CLEAR_BIT(SPI1->CR1, SPI_CR1_SSI);

			isSpiFresh = 1;
		}
	}
}

void spiSetup() {

	DMA2_Channel3->CMAR = (uint32_t) spiBuffer;
	DMA2_Channel3->CPAR = (uint32_t) LL_SPI_DMA_GetRegAddr(SPI1);
	DMA2_Channel3->CNDTR = SPI_BUF_SIZE;
	DMA2_Channel3->CCR |= DMA_CCR_EN | DMA_CCR_CIRC;

	lastDmaCndtr = SPI_BUF_SIZE;
	isSpiFresh = 1;
	spiIdleCount = 0;

	LL_SPI_EnableDMAReq_RX(SPI1);

	//listen for errors via interrupt
	//this doesn't seem necessary, OVR shouldn't happen with DMA, MODF seems master mode only, not using CRC or TI mode
//	LL_SPI_EnableIT_ERR(SPI1);

	LL_SPI_Enable(SPI1);


	// cr1.SSI is the slave select bit.
	CLEAR_BIT(SPI1->CR1, SPI_CR1_SSI);
}


void spiResetCrc() {
	//make mostly compatible with standard crc32
	CRC->CR = CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_RESET;
}


uint32_t spiGetCrc() {
	return CRC->DR ^ 0xffffffff;
}

void spiRead(void *dst, int size) {
	uint8_t *p = (uint8_t*) dst;
	while (size--)
		*p++ = spiGetc();
}

int spiAvailable() {
	int res = (SPI_BUF_SIZE - DMA2_Channel3->CNDTR) - spiPos;
	if (res < 0)
		res += SPI_BUF_SIZE;
	return res;
}

uint8_t spiGetc() {
	while (spiPos == (SPI_BUF_SIZE - DMA2_Channel3->CNDTR)) {
		//wait
	}

	uint8_t res = spiBuffer[spiPos++];
	LL_CRC_FeedData8(CRC, res);
	if (spiPos >= SPI_BUF_SIZE)
		spiPos = 0;
	return res;
}
