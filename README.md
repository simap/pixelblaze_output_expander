# USE THE APPROPRIATE BRANCH FOR YOUR HARDWARE VERSION!

* v1.x: https://github.com/simap/pixelblaze_output_expander/tree/master
* v2.x: https://github.com/simap/pixelblaze_output_expander/tree/v2.x
* v3.x: https://github.com/simap/pixelblaze_output_expander/tree/v3.x
* v4.x: this branch

The v3.x_spi and v3.x_apa102fastdata branches are variant firmwares for v3 boards. Both features are incorporated into v4.

Pixelblaze Output Expander Board
-------------------

This is a UART (serial) or SPI driven WS2811/WS2812/WS2813/NeoPixel and APA102/SK9822/DotStar driver. It receives frame data from another microcontroller, buffers it per channel, handles color ordering, and outputs up to 8 channels with level shifting to 5v. WS2812 and APA102 type LEDs can be mixed across channels.

Its primarily designed as an output expander for [Pixelblaze](https://www.tindie.com/products/12158/), but can be used with any microcontroller that can output 2Mbps serial or SPI. The UART input doesn't need to sustain 2Mbps, so this should work even for software serial implementations as long as the microcontroller is capable of outputting single characters at this rate.

See the companion [Arduino Library Driver](https://github.com/simap/pbDriverAdapter/) for use with Arduinos.

This branch is the firmware for the v4 boards, which are based on the RP2350. It also runs on RP2350 dev boards. The STM32 based v1 to v3 boards each have their own branch, linked above. The protocol is the same across all of them, the v4 boards add SPI input, larger buffers, and firmware updates over the data line.

| | v1 | v2 | v3 | v4 |
|---|---|---|---|---|
| MCU | STM32F070 | STM32F103 | STM32L432 | RP2350 |
| Input | UART 2Mbps | UART 2Mbps | UART 2Mbps | UART 2Mbps or SPI up to 20MHz |
| Bytes per channel | 720 | 2408 | 4808 | 16008 |
| WS2812 RGB pixels per channel | 240 | 802 | 1602 | 5336 |
| APA102 pixels per channel | no | 600 | 1200 | 4000 |

Input
-------------------

The data input pin takes either UART or SPI. Both receivers run all the time and the board picks whichever is active.

UART is 8N1 at 2Mbps, same as earlier boards. Only the data line is used.

SPI uses the clock input pin plus the data pin. There is no chip select. Mode 0 (clock idles low, data sampled on the rising edge), MSB first, any clock from 500kHz to 20MHz. The sender doesn't have to be continuous, bytes can arrive at any pace. If the clock stops for 10us in the middle of a byte, the partial byte is dropped and the receiver realigns on the next clock edge, so a sender should leave at least a 20us gap before each set of frames to recover from a lost or extra clock edge.

Once 8 bytes have arrived over SPI, the board switches to SPI and ignores the UART input (which sees the shared data line toggling at SPI rates). After 1 second with no SPI clock it switches back to UART.

Data Frame Format
-------------------

Frames start with a magic 4-byte sequence, followed by the channel ID (8 bits), then the command type (8 bits). Some commands have additional header/structs and payloads, finally every frame is ended with a 32-bit CRC that covers the entire frame (from magic to the last byte before CRC). All multi-byte fields are little endian.

```c
typedef struct {
	int8_t magic[4]; //"UPXL"
	uint8_t channel;
	uint8_t recordType; //set channel ws2812 opts+data, draw all
} PBFrameHeader;
```

Six commands are implemented. Four set a channel's configuration and buffer data, one draws all channels, and one carries a firmware update:

```c
enum {
	SET_CHANNEL_WS2812 = 1, DRAW_ALL, SET_CHANNEL_APA102_DATA, SET_CHANNEL_APA102_CLOCK, SET_CHANNEL_APA102_FAST_DATA, UPDATE
} RecordType;
```

The channel ID is part of the PBFrameHeader. Each board supports 8 channels, and has an additional 3 bits of address configured by cuttable jumpers on the underside of the board. Up to 64 channels can be on the same bus (8 boards w/ 8 channels each). The low 3 bits pick the channel on the board, the upper 3 bits must match the board's jumpers or the frame is ignored.

Any `SET_CHANNEL_*` command with a `pixels` count of 0 disables that channel.

### `SET_CHANNEL_WS2812`

The `SET_CHANNEL_WS2812` command frame header is immediately followed by a structure defining the configuration for that channel, and then followed by RGB or RGBW pixel data:

```c
typedef struct {
	uint8_t numElements; //3 (RGB) or 4 (RGBW)
	uint8_t redIndex :2, greenIndex :2, blueIndex :2, whiteIndex :2; //color orders, data on the line assumed to be RGB or RGBW
	uint16_t pixels;
} PBChannel;
```

In total:

```
PBFrameHeader + PBChannel + bytes[numElements * pixels] + CRC
```

Output is always at 800KHz.

### `SET_CHANNEL_APA102_DATA`

Sets a channel as the data line for APA102 type LEDs. Pixel data on the line is 4 bytes per pixel, RGB followed by a 5-bit brightness value. The color order bits work like WS2812. The start and end frames are added by the board, don't send them.

```c
typedef struct {
	uint32_t frequency; //clock rate in Hz
	uint8_t redIndex :2, greenIndex :2, blueIndex :2, unused :2;
	uint8_t reserved;
	uint16_t pixels;
} PBAPA102DataChannel;
```

In total:

```
PBFrameHeader + PBAPA102DataChannel + bytes[4 * pixels] + CRC
```

### `SET_CHANNEL_APA102_CLOCK`

Sets a channel as the clock line for APA102 type LEDs. Any number of data channels can share one clock channel, the clock runs for as many bits as the longest APA102 data channel on the board.

```c
typedef struct {
	uint32_t frequency; //clock rate in Hz
} PBAPA102ClockChannel;
```

In total:

```
PBFrameHeader + PBAPA102ClockChannel + CRC
```

The clock and all data channels are started together from the same clock divider so they stay in step. If channels ask for different frequencies, all APA102 channels on the board run at the lowest one requested, limited to the range 10KHz to 20MHz.

### `SET_CHANNEL_APA102_FAST_DATA`

Same as `SET_CHANNEL_APA102_DATA` but pixel data on the line is 2 bytes per pixel in RGB565, little endian. This halves the input bandwidth for more FPS. The board expands each pixel with a gamma curve, then uses the APA102's 5-bit brightness value to keep some dynamic range in the dark end.

```c
typedef struct {
	uint32_t frequency; //clock rate in Hz
	uint8_t redIndex :2, greenIndex :2, blueIndex :2, unused :2;
	uint8_t gammaTenths; //gamma * 10, 10 to 25 (1.0 to 2.5)
	uint16_t pixels;
} PBAPA102FastDataChannel;
```

In total:

```
PBFrameHeader + PBAPA102FastDataChannel + bytes[2 * pixels] + CRC
```

### `DRAW_ALL`

The `DRAW_ALL` command ignores the channel from the frame header, though it must still be followed by a CRC. All channels on the bus are drawn simultaneously when this command is received. This command ignores channel ID.

In total:

```
PBFrameHeader + CRC
```

### `UPDATE` (v4 boards)

Record type 6 carries a firmware update payload for the RP2350 based v4 boards. It is broadcast, so the channel byte is ignored. The frame header is followed by this header, then the payload, then the usual frame CRC:

```c
typedef struct {
	char key[64];     // hardware and payload type, NUL padded, e.g. "RP2350_ARM-2MB-standard-firmware"
	uint32_t length;  // payload bytes
	uint32_t crc;     // CRC32 of just key and length
} PBUpdate;
```

A board accepts the record only if the key is the one it was built with, so updates for other boards on the same bus are consumed and ignored. The header CRC keeps a magic sequence that happens to appear inside pixel data from locking up the parser on a bogus multi-megabyte payload. The payload is the `outputexpander.bin` from the build, which the pico-sdk seals with a SHA-256. See Firmware Updates below for what happens on the board.

Error Handling
-------------------

The firmware is designed to tolerate noise and data errors. Channel data is double-buffered. Each `SET_CHANNEL_*` frame is received into a spare buffer and only becomes the channel's pending frame once its CRC checks out. `DRAW_ALL` swaps every pending frame in and draws. A CRC mismatch throws the frame out and the channel keeps drawing its last good frame, so LEDs never see corrupted or zeroed data. Frames received during a draw go into a different buffer than the one being drawn.

The magic header provides a way to align frames in case of continuous transmission.

It's assumed that data will be more or less continuously flowing, and the reception code doesn't timeout if the input goes idle.

If part of a frame is received, it will wait for enough data to complete that frame, and eventually throw that out if the CRC doesn't match. If that is in the middle of another frame, it will discard data until the magic frame start sequence is found.

Frames with a bad or impossible header (unknown record type, more pixels than fit in a channel, zero frequency, etc) are skipped a byte at a time until the next magic sequence.

Timing Considerations
-------------------

Since channel data is double-buffered, you can send the next frame's channel data as soon as `DRAW_ALL` has been sent, while the previous frame is still going out to the LEDs.

A `DRAW_ALL` that arrives while a draw is still in progress is dropped. The same goes for a `DRAW_ALL` within 300us of the end of a WS2812 draw, since the LEDs need that time to latch. The channel data sent before it is kept and goes out on the next `DRAW_ALL`. WS2812 output takes 30us per RGB pixel (40us for RGBW) on the longest channel, plus the 300us latch, so for example 300 RGB pixels takes about 9.3ms and tops out around 107 FPS. APA102 takes 32 bits per pixel plus start and end frames at the configured clock rate.

Each channel only sends as many pixels as it was configured for, so a short channel finishes early and doesn't wait for the longest.

Status LEDs
-------------------

The status LED is on solid while frames are arriving, and breathes slowly when the input has been idle for a second. The draw LED is on while valid channel data and draw commands are both arriving.

Firmware Updates
-------------------

v4 boards can be updated over the data line with the `UPDATE` record. Flash is split into an A/B pair of 384KB slots with a partition table in the first sector. An update is written to whichever slot isn't running, then the board reboots into it. The bootrom checks the SHA-256 before running it, and falls back to the old slot if it doesn't check out, if power is lost mid-write, or if the new firmware never starts working.

The new firmware runs in try-before-you-buy mode. It commits itself (and the bootrom erases the old slot's header so the new one is preferred from then on) on the first frame it receives with a good CRC. Until then a reset goes back to the old firmware.

Updates are exact version installs. Downgrades are allowed. The only update that is skipped is one carrying the same image that is already running, compared by hash. A board gets about 384KB of RAM to hold the payload so that is the size limit. The firmware is currently about 36KB, so there is plenty of headroom.

While an update is being received the LED output stops and holds whatever was last drawn. A bad CRC on the update, or the input stopping mid-payload, reboots back into the running firmware with flash untouched.

Building
-------------------

The firmware builds with the pico-sdk (2.3.1) and CMake in the usual way with `cmake -B build && cmake --build build`. The build produces two images:

* The base image, `outputexpander.uf2` for BOOTSEL and `outputexpander_base.bin` for SWD. This is the partition table plus the firmware in slot A, with slot B cleared. Use it to prep a new board, recover a bricked one, or update manually. It overwrites whatever is on the board, partition table included. `flash_ab.sh` writes it over SWD.
* The update image, `outputexpander_update.bin`. This is the app firmware alone, built with the try-before-you-buy flag, and is the payload for the `UPDATE` record. Embed it in another microcontroller to bring attached expanders to a known version.

The base image isn't try-before-you-buy because the bootrom only starts a TBYB image after a flash update reboot, so one loaded over BOOTSEL or SWD would never run. `outputexpander.elf` and `outputexpander.bin` are the plain firmware on its own, for the debugger.

`make -C firmware/test run` builds and runs host tests for the parser and channel code.

Implementation
-------------------

Output uses one PIO state machine per channel, fed by its own DMA channel from the channel's buffer. Channels 0-3 are on PIO0 and 4-7 on PIO1, and all 8 state machines are enabled with a single register write so they start on the same clock. There is no bit transposing, each channel's buffer holds its pixels as 32-bit words with the first wire byte in the top byte and the PIO shifts them out MSB first.

The WS2812 program runs 20 PIO cycles per bit at 16MHz. The APA102 data program is 2 cycles per bit, and the APA102 clock program counts bits with the same divider, so clock and data stay aligned even on different PIO blocks.

Input DMA fills an 8KB ring buffer from UART0 and a second ring from a PIO SPI receiver on PIO2. The parser walks the rings, finds the magic, validates the header, and copies the payload out with a memory-to-memory DMA that has the CRC32 sniffer enabled, so the CPU never runs a CRC loop. Pixel data is converted to the output word format as it streams out of the ring.

Channel buffers come from a pool of 17: one being drawn and one pending per channel plus one receive target. The pool doubles as the RAM buffer for a firmware update payload.

Once set up, the CPU doesn't have to do anything while a draw is running, and the accuracy of the WS2812 signal is very good.

License Information
-------------------
The hardware files are released under [Creative Commons ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/).

Distributed as-is; no warranty is given.

The ElectroMage logo and wizard character is excluded from this license.

The software is released under The MIT License.

The MIT License

Copyright (c) 2018-2026 Ben Hencke

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
