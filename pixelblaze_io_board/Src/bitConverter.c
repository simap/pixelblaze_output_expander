/*
 * bitConverter.c
 *
 *  Created on: Oct 18, 2018
 *      Author: benh
 */


#include <stdint.h>

void bitflip8(uint8_t * out, uint8_t * in) {

	 union {
	    uint8_t bytes[4];
	    struct {
	        uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
	                 p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
	                 p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
	                 p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1;
	    };
	} o0, o1;

	uint8_t p0 = *in++;

	o1.p0d = p0 ;
	o1.p0c = p0 >> 1;
	o1.p0b = p0 >> 2;
	o1.p0a = p0 >> 3;
	o0.p0d = p0 >> 4;
	o0.p0c = p0 >> 5;
	o0.p0b = p0 >> 6;
	o0.p0a = p0 >> 7;

	uint8_t p1 = *in++;

	o1.p1d = p1 ;
	o1.p1c = p1 >> 1;
	o1.p1b = p1 >> 2;
	o1.p1a = p1 >> 3;
	o0.p1d = p1 >> 4;
	o0.p1c = p1 >> 5;
	o0.p1b = p1 >> 6;
	o0.p1a = p1 >> 7;

	uint8_t p2 = *in++;

	o1.p2d = p2 ;
	o1.p2c = p2 >> 1;
	o1.p2b = p2 >> 2;
	o1.p2a = p2 >> 3;
	o0.p2d = p2 >> 4;
	o0.p2c = p2 >> 5;
	o0.p2b = p2 >> 6;
	o0.p2a = p2 >> 7;

	uint8_t p3 = *in++;

	o1.p3d = p3 ;
	o1.p3c = p3 >> 1;
	o1.p3b = p3 >> 2;
	o1.p3a = p3 >> 3;
	o0.p3d = p3 >> 4;
	o0.p3c = p3 >> 5;
	o0.p3b = p3 >> 6;
	o0.p3a = p3 >> 7;

		uint8_t p4 = *in++;

	o1.p4d = p4 ;
	o1.p4c = p4 >> 1;
	o1.p4b = p4 >> 2;
	o1.p4a = p4 >> 3;
	o0.p4d = p4 >> 4;
	o0.p4c = p4 >> 5;
	o0.p4b = p4 >> 6;
	o0.p4a = p4 >> 7;

		uint8_t p5 = *in++;

	o1.p5d = p5 ;
	o1.p5c = p5 >> 1;
	o1.p5b = p5 >> 2;
	o1.p5a = p5 >> 3;
	o0.p5d = p5 >> 4;
	o0.p5c = p5 >> 5;
	o0.p5b = p5 >> 6;
	o0.p5a = p5 >> 7;

		uint8_t p6 = *in++;

	o1.p6d = p6 ;
	o1.p6c = p6 >> 1;
	o1.p6b = p6 >> 2;
	o1.p6a = p6 >> 3;
	o0.p6d = p6 >> 4;
	o0.p6c = p6 >> 5;
	o0.p6b = p6 >> 6;
	o0.p6a = p6 >> 7;

		uint8_t p7 = *in++;

	o1.p7d = p7 ;
	o1.p7c = p7 >> 1;
	o1.p7b = p7 >> 2;
	o1.p7a = p7 >> 3;
	o0.p7d = p7 >> 4;
	o0.p7c = p7 >> 5;
	o0.p7b = p7 >> 6;
	o0.p7a = p7 >> 7;

	*out++ = o0.bytes[0];
	*out++ = o0.bytes[1];
	*out++ = o0.bytes[2];
	*out++ = o0.bytes[3];
	*out++ = o1.bytes[0];
	*out++ = o1.bytes[1];
	*out++ = o1.bytes[2];
	*out++ = o1.bytes[3];
}


void foo2(uint32_t * out, uint32_t * in) {

    // Six output words
    union {
        uint32_t word;
        struct {
            uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
                     p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
                     p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
                     p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1;
        };
    } o0, o1, o2, o3, o4, o5;

    /*
     * Remap bits.
     *
     * This generates compact and efficient code using the BFI instruction.
     */

    uint32_t p0 = *in++;

    o5.p0d = p0;
    o5.p0c = p0 >> 1;
    o5.p0b = p0 >> 2;
    o5.p0a = p0 >> 3;
    o4.p0d = p0 >> 4;
    o4.p0c = p0 >> 5;
    o4.p0b = p0 >> 6;
    o4.p0a = p0 >> 7;
    o3.p0d = p0 >> 8;
    o3.p0c = p0 >> 9;
    o3.p0b = p0 >> 10;
    o3.p0a = p0 >> 11;
    o2.p0d = p0 >> 12;
    o2.p0c = p0 >> 13;
    o2.p0b = p0 >> 14;
    o2.p0a = p0 >> 15;
    o1.p0d = p0 >> 16;
    o1.p0c = p0 >> 17;
    o1.p0b = p0 >> 18;
    o1.p0a = p0 >> 19;
    o0.p0d = p0 >> 20;
    o0.p0c = p0 >> 21;
    o0.p0b = p0 >> 22;
    o0.p0a = p0 >> 23;

    uint32_t p1 = *in++;

    o5.p1d = p1;
    o5.p1c = p1 >> 1;
    o5.p1b = p1 >> 2;
    o5.p1a = p1 >> 3;
    o4.p1d = p1 >> 4;
    o4.p1c = p1 >> 5;
    o4.p1b = p1 >> 6;
    o4.p1a = p1 >> 7;
    o3.p1d = p1 >> 8;
    o3.p1c = p1 >> 9;
    o3.p1b = p1 >> 10;
    o3.p1a = p1 >> 11;
    o2.p1d = p1 >> 12;
    o2.p1c = p1 >> 13;
    o2.p1b = p1 >> 14;
    o2.p1a = p1 >> 15;
    o1.p1d = p1 >> 16;
    o1.p1c = p1 >> 17;
    o1.p1b = p1 >> 18;
    o1.p1a = p1 >> 19;
    o0.p1d = p1 >> 20;
    o0.p1c = p1 >> 21;
    o0.p1b = p1 >> 22;
    o0.p1a = p1 >> 23;

    uint32_t p2 = *in++;

    o5.p2d = p2;
    o5.p2c = p2 >> 1;
    o5.p2b = p2 >> 2;
    o5.p2a = p2 >> 3;
    o4.p2d = p2 >> 4;
    o4.p2c = p2 >> 5;
    o4.p2b = p2 >> 6;
    o4.p2a = p2 >> 7;
    o3.p2d = p2 >> 8;
    o3.p2c = p2 >> 9;
    o3.p2b = p2 >> 10;
    o3.p2a = p2 >> 11;
    o2.p2d = p2 >> 12;
    o2.p2c = p2 >> 13;
    o2.p2b = p2 >> 14;
    o2.p2a = p2 >> 15;
    o1.p2d = p2 >> 16;
    o1.p2c = p2 >> 17;
    o1.p2b = p2 >> 18;
    o1.p2a = p2 >> 19;
    o0.p2d = p2 >> 20;
    o0.p2c = p2 >> 21;
    o0.p2b = p2 >> 22;
    o0.p2a = p2 >> 23;

    uint32_t p3 = *in++;

    o5.p3d = p3;
    o5.p3c = p3 >> 1;
    o5.p3b = p3 >> 2;
    o5.p3a = p3 >> 3;
    o4.p3d = p3 >> 4;
    o4.p3c = p3 >> 5;
    o4.p3b = p3 >> 6;
    o4.p3a = p3 >> 7;
    o3.p3d = p3 >> 8;
    o3.p3c = p3 >> 9;
    o3.p3b = p3 >> 10;
    o3.p3a = p3 >> 11;
    o2.p3d = p3 >> 12;
    o2.p3c = p3 >> 13;
    o2.p3b = p3 >> 14;
    o2.p3a = p3 >> 15;
    o1.p3d = p3 >> 16;
    o1.p3c = p3 >> 17;
    o1.p3b = p3 >> 18;
    o1.p3a = p3 >> 19;
    o0.p3d = p3 >> 20;
    o0.p3c = p3 >> 21;
    o0.p3b = p3 >> 22;
    o0.p3a = p3 >> 23;

    uint32_t p4 = *in++;

    o5.p4d = p4;
    o5.p4c = p4 >> 1;
    o5.p4b = p4 >> 2;
    o5.p4a = p4 >> 3;
    o4.p4d = p4 >> 4;
    o4.p4c = p4 >> 5;
    o4.p4b = p4 >> 6;
    o4.p4a = p4 >> 7;
    o3.p4d = p4 >> 8;
    o3.p4c = p4 >> 9;
    o3.p4b = p4 >> 10;
    o3.p4a = p4 >> 11;
    o2.p4d = p4 >> 12;
    o2.p4c = p4 >> 13;
    o2.p4b = p4 >> 14;
    o2.p4a = p4 >> 15;
    o1.p4d = p4 >> 16;
    o1.p4c = p4 >> 17;
    o1.p4b = p4 >> 18;
    o1.p4a = p4 >> 19;
    o0.p4d = p4 >> 20;
    o0.p4c = p4 >> 21;
    o0.p4b = p4 >> 22;
    o0.p4a = p4 >> 23;

    uint32_t p5 = *in++;

    o5.p5d = p5;
    o5.p5c = p5 >> 1;
    o5.p5b = p5 >> 2;
    o5.p5a = p5 >> 3;
    o4.p5d = p5 >> 4;
    o4.p5c = p5 >> 5;
    o4.p5b = p5 >> 6;
    o4.p5a = p5 >> 7;
    o3.p5d = p5 >> 8;
    o3.p5c = p5 >> 9;
    o3.p5b = p5 >> 10;
    o3.p5a = p5 >> 11;
    o2.p5d = p5 >> 12;
    o2.p5c = p5 >> 13;
    o2.p5b = p5 >> 14;
    o2.p5a = p5 >> 15;
    o1.p5d = p5 >> 16;
    o1.p5c = p5 >> 17;
    o1.p5b = p5 >> 18;
    o1.p5a = p5 >> 19;
    o0.p5d = p5 >> 20;
    o0.p5c = p5 >> 21;
    o0.p5b = p5 >> 22;
    o0.p5a = p5 >> 23;

    uint32_t p6 = *in++;

    o5.p6d = p6;
    o5.p6c = p6 >> 1;
    o5.p6b = p6 >> 2;
    o5.p6a = p6 >> 3;
    o4.p6d = p6 >> 4;
    o4.p6c = p6 >> 5;
    o4.p6b = p6 >> 6;
    o4.p6a = p6 >> 7;
    o3.p6d = p6 >> 8;
    o3.p6c = p6 >> 9;
    o3.p6b = p6 >> 10;
    o3.p6a = p6 >> 11;
    o2.p6d = p6 >> 12;
    o2.p6c = p6 >> 13;
    o2.p6b = p6 >> 14;
    o2.p6a = p6 >> 15;
    o1.p6d = p6 >> 16;
    o1.p6c = p6 >> 17;
    o1.p6b = p6 >> 18;
    o1.p6a = p6 >> 19;
    o0.p6d = p6 >> 20;
    o0.p6c = p6 >> 21;
    o0.p6b = p6 >> 22;
    o0.p6a = p6 >> 23;

    uint32_t p7 = *in++;

    o5.p7d = p7;
    o5.p7c = p7 >> 1;
    o5.p7b = p7 >> 2;
    o5.p7a = p7 >> 3;
    o4.p7d = p7 >> 4;
    o4.p7c = p7 >> 5;
    o4.p7b = p7 >> 6;
    o4.p7a = p7 >> 7;
    o3.p7d = p7 >> 8;
    o3.p7c = p7 >> 9;
    o3.p7b = p7 >> 10;
    o3.p7a = p7 >> 11;
    o2.p7d = p7 >> 12;
    o2.p7c = p7 >> 13;
    o2.p7b = p7 >> 14;
    o2.p7a = p7 >> 15;
    o1.p7d = p7 >> 16;
    o1.p7c = p7 >> 17;
    o1.p7b = p7 >> 18;
    o1.p7a = p7 >> 19;
    o0.p7d = p7 >> 20;
    o0.p7c = p7 >> 21;
    o0.p7b = p7 >> 22;
    o0.p7a = p7 >> 23;

    *(out++) = o0.word;
    *(out++) = o1.word;
    *(out++) = o2.word;
    *(out++) = o3.word;
    *(out++) = o4.word;
    *(out++) = o5.word;
}
