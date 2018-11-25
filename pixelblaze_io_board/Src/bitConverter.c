/*
 * bitConverter.c
 *
 *  Created on: Oct 18, 2018
 *      Author: benh
 */

#include <stdint.h>

union b32 {
	uint32_t word;
	struct {
		uint32_t p0a :1, p1a :1, p2a :1, p3a :1, p4a :1, p5a :1, p6a :1, p7a :1,
				p0b :1, p1b :1, p2b :1, p3b :1, p4b :1, p5b :1, p6b :1, p7b :1,
				p0c :1, p1c :1, p2c :1, p3c :1, p4c :1, p5c :1, p6c :1, p7c :1,
				p0d :1, p1d :1, p2d :1, p3d :1, p4d :1, p5d :1, p6d :1, p7d :1;
	};
} o0, o1;



void bitMangler0(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p0d = in;
	o1->p0c = in >> 1;
	o1->p0b = in >> 2;
	o1->p0a = in >> 3;
	o0->p0d = in >> 4;
	o0->p0c = in >> 5;
	o0->p0b = in >> 6;
	o0->p0a = in >> 7;
}
void bitMangler1(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p1d = in;
	o1->p1c = in >> 1;
	o1->p1b = in >> 2;
	o1->p1a = in >> 3;
	o0->p1d = in >> 4;
	o0->p1c = in >> 5;
	o0->p1b = in >> 6;
	o0->p1a = in >> 7;
}
void bitMangler2(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p2d = in;
	o1->p2c = in >> 1;
	o1->p2b = in >> 2;
	o1->p2a = in >> 3;
	o0->p2d = in >> 4;
	o0->p2c = in >> 5;
	o0->p2b = in >> 6;
	o0->p2a = in >> 7;
}
void bitMangler3(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p3d = in;
	o1->p3c = in >> 1;
	o1->p3b = in >> 2;
	o1->p3a = in >> 3;
	o0->p3d = in >> 4;
	o0->p3c = in >> 5;
	o0->p3b = in >> 6;
	o0->p3a = in >> 7;
}
void bitMangler4(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p4d = in;
	o1->p4c = in >> 1;
	o1->p4b = in >> 2;
	o1->p4a = in >> 3;
	o0->p4d = in >> 4;
	o0->p4c = in >> 5;
	o0->p4b = in >> 6;
	o0->p4a = in >> 7;
}
void bitMangler5(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p5d = in;
	o1->p5c = in >> 1;
	o1->p5b = in >> 2;
	o1->p5a = in >> 3;
	o0->p5d = in >> 4;
	o0->p5c = in >> 5;
	o0->p5b = in >> 6;
	o0->p5a = in >> 7;
}
void bitMangler6(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p6d = in;
	o1->p6c = in >> 1;
	o1->p6b = in >> 2;
	o1->p6a = in >> 3;
	o0->p6d = in >> 4;
	o0->p6c = in >> 5;
	o0->p6b = in >> 6;
	o0->p6a = in >> 7;
}
void bitMangler7(uint32_t * out, uint8_t in) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	o1->p7d = in;
	o1->p7c = in >> 1;
	o1->p7b = in >> 2;
	o1->p7a = in >> 3;
	o0->p7d = in >> 4;
	o0->p7c = in >> 5;
	o0->p7b = in >> 6;
	o0->p7a = in >> 7;
}

void bitMangler(uint32_t * out, uint8_t in, uint8_t pos) {
	union b32 *o0, *o1;
	o0 = (union b32*) out;
	o1 = (union b32*) &out[1];
	switch (pos) {
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
	}
}

//void bitMangler(uint32_t * out, uint8_t in, uint8_t pos) {
//	union b32  o0, o1;
//	o0.word = out[0];
//	o1.word = out[1];
//	switch (pos) {
//	case 0:
//		o1.p0d = in;
//		o1.p0c = in >> 1;
//		o1.p0b = in >> 2;
//		o1.p0a = in >> 3;
//		o0.p0d = in >> 4;
//		o0.p0c = in >> 5;
//		o0.p0b = in >> 6;
//		o0.p0a = in >> 7;
//		break;
//	case 1:
//		o1.p1d = in;
//		o1.p1c = in >> 1;
//		o1.p1b = in >> 2;
//		o1.p1a = in >> 3;
//		o0.p1d = in >> 4;
//		o0.p1c = in >> 5;
//		o0.p1b = in >> 6;
//		o0.p1a = in >> 7;
//		break;
//	case 2:
//		o1.p2d = in;
//		o1.p2c = in >> 1;
//		o1.p2b = in >> 2;
//		o1.p2a = in >> 3;
//		o0.p2d = in >> 4;
//		o0.p2c = in >> 5;
//		o0.p2b = in >> 6;
//		o0.p2a = in >> 7;
//		break;
//	case 3:
//		o1.p3d = in;
//		o1.p3c = in >> 1;
//		o1.p3b = in >> 2;
//		o1.p3a = in >> 3;
//		o0.p3d = in >> 4;
//		o0.p3c = in >> 5;
//		o0.p3b = in >> 6;
//		o0.p3a = in >> 7;
//		break;
//	case 4:
//		o1.p4d = in;
//		o1.p4c = in >> 1;
//		o1.p4b = in >> 2;
//		o1.p4a = in >> 3;
//		o0.p4d = in >> 4;
//		o0.p4c = in >> 5;
//		o0.p4b = in >> 6;
//		o0.p4a = in >> 7;
//		break;
//	case 5:
//		o1.p5d = in;
//		o1.p5c = in >> 1;
//		o1.p5b = in >> 2;
//		o1.p5a = in >> 3;
//		o0.p5d = in >> 4;
//		o0.p5c = in >> 5;
//		o0.p5b = in >> 6;
//		o0.p5a = in >> 7;
//		break;
//	case 6:
//		o1.p6d = in;
//		o1.p6c = in >> 1;
//		o1.p6b = in >> 2;
//		o1.p6a = in >> 3;
//		o0.p6d = in >> 4;
//		o0.p6c = in >> 5;
//		o0.p6b = in >> 6;
//		o0.p6a = in >> 7;
//		break;
//	case 7:
//		o1.p7d = in;
//		o1.p7c = in >> 1;
//		o1.p7b = in >> 2;
//		o1.p7a = in >> 3;
//		o0.p7d = in >> 4;
//		o0.p7c = in >> 5;
//		o0.p7b = in >> 6;
//		o0.p7a = in >> 7;
//		break;
//	}
//	out[0] = o0.word;
//	out[1] = o1.word;
//}

void bitflip8(uint8_t * out, uint8_t * in) {

	union {
		uint8_t bytes[4];
		struct {
			uint32_t p0a :1, p1a :1, p2a :1, p3a :1, p4a :1, p5a :1, p6a :1,
					p7a :1, p0b :1, p1b :1, p2b :1, p3b :1, p4b :1, p5b :1,
					p6b :1, p7b :1, p0c :1, p1c :1, p2c :1, p3c :1, p4c :1,
					p5c :1, p6c :1, p7c :1, p0d :1, p1d :1, p2d :1, p3d :1,
					p4d :1, p5d :1, p6d :1, p7d :1;
		};
	} o0, o1;

	uint8_t p0 = *in++;

	o1.p0d = p0;
	o1.p0c = p0 >> 1;
	o1.p0b = p0 >> 2;
	o1.p0a = p0 >> 3;
	o0.p0d = p0 >> 4;
	o0.p0c = p0 >> 5;
	o0.p0b = p0 >> 6;
	o0.p0a = p0 >> 7;

	uint8_t p1 = *in++;

	o1.p1d = p1;
	o1.p1c = p1 >> 1;
	o1.p1b = p1 >> 2;
	o1.p1a = p1 >> 3;
	o0.p1d = p1 >> 4;
	o0.p1c = p1 >> 5;
	o0.p1b = p1 >> 6;
	o0.p1a = p1 >> 7;

	uint8_t p2 = *in++;

	o1.p2d = p2;
	o1.p2c = p2 >> 1;
	o1.p2b = p2 >> 2;
	o1.p2a = p2 >> 3;
	o0.p2d = p2 >> 4;
	o0.p2c = p2 >> 5;
	o0.p2b = p2 >> 6;
	o0.p2a = p2 >> 7;

	uint8_t p3 = *in++;

	o1.p3d = p3;
	o1.p3c = p3 >> 1;
	o1.p3b = p3 >> 2;
	o1.p3a = p3 >> 3;
	o0.p3d = p3 >> 4;
	o0.p3c = p3 >> 5;
	o0.p3b = p3 >> 6;
	o0.p3a = p3 >> 7;

	uint8_t p4 = *in++;

	o1.p4d = p4;
	o1.p4c = p4 >> 1;
	o1.p4b = p4 >> 2;
	o1.p4a = p4 >> 3;
	o0.p4d = p4 >> 4;
	o0.p4c = p4 >> 5;
	o0.p4b = p4 >> 6;
	o0.p4a = p4 >> 7;

	uint8_t p5 = *in++;

	o1.p5d = p5;
	o1.p5c = p5 >> 1;
	o1.p5b = p5 >> 2;
	o1.p5a = p5 >> 3;
	o0.p5d = p5 >> 4;
	o0.p5c = p5 >> 5;
	o0.p5b = p5 >> 6;
	o0.p5a = p5 >> 7;

	uint8_t p6 = *in++;

	o1.p6d = p6;
	o1.p6c = p6 >> 1;
	o1.p6b = p6 >> 2;
	o1.p6a = p6 >> 3;
	o0.p6d = p6 >> 4;
	o0.p6c = p6 >> 5;
	o0.p6b = p6 >> 6;
	o0.p6a = p6 >> 7;

	uint8_t p7 = *in;

	o1.p7d = p7;
	o1.p7c = p7 >> 1;
	o1.p7b = p7 >> 2;
	o1.p7a = p7 >> 3;
	o0.p7d = p7 >> 4;
	o0.p7c = p7 >> 5;
	o0.p7b = p7 >> 6;
	o0.p7a = p7 >> 7;

//	memcpy(out, o0.bytes, 4);
//	memcpy(out +4, o1.bytes, 4);

	*out++ = o0.bytes[0];
	*out++ = o0.bytes[1];
	*out++ = o0.bytes[2];
	*out++ = o0.bytes[3];
	*out++ = o1.bytes[0];
	*out++ = o1.bytes[1];
	*out++ = o1.bytes[2];
	*out++ = o1.bytes[3];
}

void bitCopyMsb(uint32_t *dst, uint8_t dstBit, uint8_t data) {
	switch (dstBit) {
	case 0:
		bitMangler0(dst, data);
		break;
	case 1:
		bitMangler1(dst, data);
		break;
	case 2:
		bitMangler2(dst, data);
		break;
	case 3:
		bitMangler3(dst, data);
		break;
	case 4:
		bitMangler4(dst, data);
		break;
	case 5:
		bitMangler5(dst, data);
		break;
	case 6:
		bitMangler6(dst, data);
		break;
	case 7:
		bitMangler7(dst, data);
		break;
	default:
		break;
	}
}

/**
 * Copy a serial bit stream, MSB first, from src to an 8-bit parallel bitstream dst.
 */
void bitsCopyMsb(uint32_t *dst, uint8_t dstBit, uint8_t *src, int len) {
	switch (dstBit) {
	case 0:
		while (len--) {
			bitMangler0(dst, *src++);
			dst += 2;
		}
		break;
	case 1:
		while (len--) {
			bitMangler1(dst, *src++);
			dst += 2;
		}
		break;
	case 2:
		while (len--) {
			bitMangler2(dst, *src++);
			dst += 2;
		}
		break;
	case 3:
		while (len--) {
			bitMangler3(dst, *src++);
			dst += 2;
		}
		break;
	case 4:
		while (len--) {
			bitMangler4(dst, *src++);
			dst += 2;
		}
		break;
	case 5:
		while (len--) {
			bitMangler5(dst, *src++);
			dst += 2;
		}
		break;
	case 6:
		while (len--) {
			bitMangler6(dst, *src++);
			dst += 2;
		}
		break;
	case 7:
		while (len--) {
			bitMangler7(dst, *src++);
			dst += 2;
		}
		break;
	}
}
