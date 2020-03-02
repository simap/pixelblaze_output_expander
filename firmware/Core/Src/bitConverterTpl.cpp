#include <stdint.h>

#include "main.h"
#include "app.h"


//#define NOPHACK __NOP(); __NOP(); __NOP()
#define NOPHACK

#define B0_ZERO_MASK 0b11111110111111101111111011111110
#define B1_ZERO_MASK 0b11111101111111011111110111111101
#define B2_ZERO_MASK 0b11111011111110111111101111111011
#define B3_ZERO_MASK 0b11110111111101111111011111110111
#define B4_ZERO_MASK 0b11101111111011111110111111101111
#define B5_ZERO_MASK 0b11011111110111111101111111011111
#define B6_ZERO_MASK 0b10111111101111111011111110111111
#define B7_ZERO_MASK 0b01111111011111110111111101111111

const static uint32_t bitMasks[] = {
		0b11111110111111101111111011111110,
		0b11111101111111011111110111111101,
		0b11111011111110111111101111111011,
		0b11110111111101111111011111110111,
		0b11101111111011111110111111101111,
		0b11011111110111111101111111011111,
		0b10111111101111111011111110111111,
		0b01111111011111110111111101111111
};

union b32 {
	uint32_t word;
	struct {
		uint32_t p0a :1, p1a :1, p2a :1, p3a :1, p4a :1, p5a :1, p6a :1, p7a :1,
				p0b :1, p1b :1, p2b :1, p3b :1, p4b :1, p5b :1, p6b :1, p7b :1,
				p0c :1, p1c :1, p2c :1, p3c :1, p4c :1, p5c :1, p6c :1, p7c :1,
				p0d :1, p1d :1, p2d :1, p3d :1, p4d :1, p5d :1, p6d :1, p7d :1;
	};
};


//assumes 8 byte blocks, size in 8 byte blocks
void bitSetZeros(uint32_t *dst, uint8_t channel, int numBlocks) {
	uint32_t *o0 = dst;
	uint32_t *o1 = dst + numBlocks;
	const uint32_t mask = bitMasks[channel];
	while (numBlocks--) {
		*o0++ &= mask;
		*o1++ &= mask;
	}
}

//assumes 8 byte blocks, size in 8 byte blocks
void bitSetOnes(uint32_t *dst, uint8_t channel, int numBlocks) {
	uint32_t *o0 = dst;
	uint32_t *o1 = dst + numBlocks;
	const uint32_t mask = ~bitMasks[channel];
	while (numBlocks--) {
		*o0++ |= mask;
		*o1++ |= mask;
	}
}

template<uint8_t C>
void bitConverterT(register uint32_t *dst, register uint8_t *data, register int size) {
	register union b32 *o0, *o1;
	o0 = (union b32*) dst;
	o1 = (union b32*) dst+1;
	register uint8_t in;
	while (size--) {
		NOPHACK;
		in = *data++;
		NOPHACK;
		switch (C) {
		case 0:
			o1->p0d = in;
			o1->p0c = in >> 1;
			o1->p0b = in >> 2;
			o1->p0a = in >> 3;
			o0->p0d = in >> 4;
			o0->p0c = in >> 5;
			o0->p0b = in >> 6;
			o0->p0a = in >> 7;
			break;
		case 1:
			o1->p1d = in;
			o1->p1c = in >> 1;
			o1->p1b = in >> 2;
			o1->p1a = in >> 3;
			o0->p1d = in >> 4;
			o0->p1c = in >> 5;
			o0->p1b = in >> 6;
			o0->p1a = in >> 7;
			break;
		case 2:
			o1->p2d = in;
			o1->p2c = in >> 1;
			o1->p2b = in >> 2;
			o1->p2a = in >> 3;
			o0->p2d = in >> 4;
			o0->p2c = in >> 5;
			o0->p2b = in >> 6;
			o0->p2a = in >> 7;
			break;
		case 3:
			o1->p3d = in;
			o1->p3c = in >> 1;
			o1->p3b = in >> 2;
			o1->p3a = in >> 3;
			o0->p3d = in >> 4;
			o0->p3c = in >> 5;
			o0->p3b = in >> 6;
			o0->p3a = in >> 7;
			break;
		case 4:
			o1->p4d = in;
			o1->p4c = in >> 1;
			o1->p4b = in >> 2;
			o1->p4a = in >> 3;
			o0->p4d = in >> 4;
			o0->p4c = in >> 5;
			o0->p4b = in >> 6;
			o0->p4a = in >> 7;
			break;
		case 5:
			o1->p5d = in;
			o1->p5c = in >> 1;
			o1->p5b = in >> 2;
			o1->p5a = in >> 3;
			o0->p5d = in >> 4;
			o0->p5c = in >> 5;
			o0->p5b = in >> 6;
			o0->p5a = in >> 7;
			break;
		case 6:
			o1->p6d = in;
			o1->p6c = in >> 1;
			o1->p6b = in >> 2;
			o1->p6a = in >> 3;
			o0->p6d = in >> 4;
			o0->p6c = in >> 5;
			o0->p6b = in >> 6;
			o0->p6a = in >> 7;
			break;
		case 7:
			o1->p7d = in;
			o1->p7c = in >> 1;
			o1->p7b = in >> 2;
			o1->p7a = in >> 3;
			o0->p7d = in >> 4;
			o0->p7c = in >> 5;
			o0->p7b = in >> 6;
			o0->p7a = in >> 7;
			break;
		default:
			break;
		}
		NOPHACK;
		o0 += 2;
		NOPHACK;
		o1 += 2;
		NOPHACK;
	}
}
#ifdef __cplusplus
extern "C" {
#endif

void bitConverter(uint32_t *dst, uint8_t dstBit, uint8_t *data, int size) {
	switch (dstBit) {
	case 0:
		bitConverterT<0>(dst, data, size);
		break;
	case 1:
		bitConverterT<1>(dst, data, size);
		break;
	case 2:
		bitConverterT<2>(dst, data, size);
		break;
	case 3:
		bitConverterT<3>(dst, data, size);
		break;
	case 4:
		bitConverterT<4>(dst, data, size);
		break;
	case 5:
		bitConverterT<5>(dst, data, size);
		break;
	case 6:
		bitConverterT<6>(dst, data, size);
		break;
	case 7:
		bitConverterT<7>(dst, data, size);
		break;
	default:
		break;
	}
}

#ifdef __cplusplus
}
#endif
