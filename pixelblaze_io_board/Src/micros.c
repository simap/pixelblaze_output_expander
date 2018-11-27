#include "main.h"
#include "app.h"


volatile uint32_t microsOverflow;

uint32_t micros() {
	return TIM14->CNT | (microsOverflow<<16);
}

