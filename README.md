# USE THE APPROPRIATE BRANCH FOR YOUR HARDWARE VERSION!

* v1.x: this branch
* v2.x: https://github.com/simap/pixelblaze_output_expander/tree/v2.x
* v3.x: https://github.com/simap/pixelblaze_output_expander/tree/v3.x

Pixelblaze Output Expander Board
-------------------

This is a UART (serial) driven WS2811/WS2812/WS2813/NeoPixel driver. It receives serial data from another microcontroller at 2Mbps, buffers this data up to 240 RGB or 180 RGBW pixels per channel, handles color ordering, and outputs up to 8 channels with level shifting to 5v.

Its primarily designed as an output expander for [Pixelblaze](https://www.tindie.com/products/12158/), but can be used with any microcontroller that can output 2Mbps serial. It doesn't need to sustain 2Mbps, so this should work even for software serial implementations as long as the microcontroller is capable of outputting single characters at this rate.

See the companion [Arduino Library Driver](https://github.com/simap/pbDriverAdapter/) for use with Arduinos.

Data Frame Format
-------------------

Frames start with a magic 4-byte sequence, followed by the channel ID (8 bits), then the command type (8 bits). Some commands have additional header/structs and payloads, finally every frame is ended with a 32-bit CRC that covers the entire frame (from magic to the last byte before CRC).

```c
typedef struct {
	int8_t magic[4]; //"UPXL"
	uint8_t channel;
	uint8_t recordType; //set channel ws2812 opts+data, draw all
} PBFrameHeader;
```

Two commands are implemented, one sets a channel's configuration and buffer data, and the other draws all channels:

```c
enum {
	SET_CHANNEL_WS2812 = 1, DRAW_ALL
} RecordType;
```

### `SET_CHANNEL_WS2812`

The channel ID is part of the PBFrameHeader. Each board supports 8 channels, and has an additional 3 bits of address configured by cuttable jumpers on the underside of the board. Up to 64 channels can be on the same bus (8 boards w/ 8 channels each).

The `SET_CHANNEL_WS2812` command frame header is immediately followed by a structure defining the configuration for that channel, and then followed by RGB or RGBW pixel data:

```c
typedef struct {
	uint8_t numElements; //0 to disable channel, usually 3 (RGB) or 4 (RGBW)
	uint8_t redIndex :2, greenIndex :2, blueIndex :2, whiteIndex :2; //color orders, data on the line assumed to be RGB or RGBW
	uint16_t pixels;
} PBChannel;
```

In total:

```
PBFrameHeader + PBChannel + bytes[numElements * pixels] + CRC
```

### `DRAW_ALL`

The `DRAW_ALL` command ignores the channel from the frame header, though it must still be followed by a CRC. All channels on the bus are drawn simultaneously when this command is received. This command ignores channel ID.

In total:

```
PBFrameHeader + CRC
```

Error Handling
-------------------

The firmware is designed to tolerate noise and data errors. There isn't enough memory to double-buffer the channel data, but any channel data received is zeroed out in case the CRC does not match.

The magic header provides a way to align frames in case of continuous transmission.

It's assumed that data will be more or less continuously flowing, and the reception code doesn't timeout if the input goes idle.

If part of a frame is received, it will wait for enough data to complete that frame, and eventually throw that out if the CRC doesn't match. If that is in the middle of another frame, it will discard data until the magic frame start sequence is found. 

Timing Considerations
-------------------

There isn't enough memory to double-buffer the channel data, so its possible to send a draw command, and then immediately follow with channel data that will update the buffer as it is being drawn. Usually this is not a problem, but if corrupted data or pixels are very different, it might cause a momentary display glitch until the next frame is drawn.

To avoid any potential issues, you can delay sending the next channel data until drawing is completed. Drawing takes 5760 bit times at 800Khz, or 7.2ms. Given that the input data rate is 2Mbps (and that includes start/stop bits, so effectively 1.6Mbps of data), its possible to delay for less than this as input would be updating buffer area that has already been drawn. Waiting for 3.6ms would give the drawing operation enough of a head start that an `SET_CHANNEL_WS2812` frame won't overtake it. Even if corrupted data is written, it won't get displayed as the CRC mismatch will cause the buffer to be cleared and the channel to be disabled until valid data is sent.

Implementation
-------------------

The implementation is based on [OctoWS2811](https://www.pjrc.com/teensy/td_libs_OctoWS2811.html) and [FadeCandy](https://github.com/scanlime/fadecandy).

Data to be sent out to the LEDs is assembled into bit positions, and then timers and DMA are used to "bit bang" this out on a GPIO port, 8 channels at a time.

TIM1 is set up in slave mode and is gated to TIM3. As long as TIM3 is active, TIM1 is running.

TIM1 has 3 PWM/CC channels set up, each triggers a different DMA channel to fire. 

3 DMA channels are set up:

1. Copies the start bits to the GPIO port. Disabled channels have a 0 for their start bit, so the output remains low, while active channels start a pulse. This is set in circular mode with a length of 1 - basically a "set" command every time this fires.
2. Copies from the data buffer to the GPIO port. This continues the start bit for '1's, or cuts it short for '0's. Disabled channels have all of their data bits zeroed out. DMA transfer length is the number of bytes in the buffer: 5760. Enough for 720 bytes across 8 channels.
3. Copies 8 bits of '1's to the GPIO port reset register, turning off any remaining pulses. This is also set in circular mode with a length of 1.

TIM3 is set up as one long pulse, gating TIM1, and runs for long enough to transmit all of the bits in the buffer.

This all happens with automatic timers and DMA, the CPU doesn't have to do anything once it's set up to start, and the accuracy of the WS2812 signal is very good.

License Information
-------------------
The hardware files are released under [Creative Commons ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/).

Distributed as-is; no warranty is given.

The ElectroMage logo and wizard character is excluded from this license.

The software is released under The MIT License.

The MIT License

Copyright (c) 2018 Ben Hencke

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
