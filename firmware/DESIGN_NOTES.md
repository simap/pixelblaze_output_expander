# RP2354 port design notes

Notes from 2026-11-02. Branch v4.x, new hardware.

## Goals

- Port the output expander firmware to the RP2354.
- Use PIO for WS2812 and APA102 type LED output, with types mixed across channels.
- Raise the input data rate to about 4 Mbaud or more, with auto baud detect.
- Support SPI input.

Round 2 ideas:
- Push firmware upgrades.
- Add LED types: WS2816, HD108/NS108, UCS5603, UCS7604
- USB (CDC, custom device, or display, MSC for config?)
- Could push color correction/gamma to board.
- Could push power limit to board/channel. 
- Temporal dithering mode for 8 bit LEDs when channel pixel count supports it.
- Low data FPS keyframe interpolation.
- Jumperless board address assining protocol maybe. Would have to rely on eyeballs. hash(serial + try) until they show different patterns, then assign address.

## Board pins

| Function | GPIO |
|---|---|
| LED outputs, channels 0 to 7 | 0 to 7 |
| Address jumper 3 | 13 |
| Address jumper 2 | 14 |
| Address jumper 1 | 15 |
| Input data | 19 and 20 |
| Input clock | 22 |
| Status LED, active high | 21 |
| Draw LED, active high | 23 |

Chip is an RP2354A (QFN-60). Flash is the 2 MB die in the package.

## Input

Data arrives on the input pins as either UART at 2 Mbaud or higher, or SPI at arbitrary speed.

UART rates planned: 2 Mbaud default, then 3, 4, 5. ESP32 tops out at 5MBaud. The 0.06% rate error at 3 Mbaud is marginal next to signal quality. If a link has to drop from 4 to 3, that is still better than 2.

SPI is the path for higher clocks. On the Pixelblaze side the ESP32 uses GPIO18 for clock and GPIO23 for data.

Wiring, bus layout, and fanout will set the practical speed. CRC32 catches the small errors. Boards are sometimes less than an inch apart, so high clocks should be possible. The cap is on the receive side. Pi's 12.5 MHz SPI effective bandwidth is almost 8x the original 2 Mbaud.

8 output channels at 800 kbps is 6.4 Mbps per board, so a 12.5 Mbps bus has headroom for about 2 boards kept close to saturated.

## Bit conversion

Bit conversion for the parallel output was the CPU bottleneck on the STM32 board. At 80 MHz it measured around 4.5 Mbaud worth of conversion. It can be split across cores if needed. Cortex-M33 instructions or the way PIO works may get around it. Otherwise one 150 MHz core dedicated to conversion would top out around 6.75 Mbps of SPI input.

## Firmware update

- Firmware is tiny, could buffer entirely in RAM.
- Update frame covers whole image.
- Use A/B slots.
- Match key on hardware (in image flash). Ignore firmware for other hardware.
- Variants self-selct, no need for OTP.
- Reboot on upgrade failure, back to working LEDs.
- Frame has CRC32, firmare image can have sha256.
- Update is exact version, so a downgrade or older version of PB could downgrade the board. Only skip same-version + same sha256, so a limping board could repair. Plus keeps local dev sane without incrementing versions every build.
- If there is a reason to refuse downgrade, the newer board firmware could decide, but would have to satisfy old protocol requirements.
- Broadcast to all boards, since it still keys on hardware key. No reason to update just one by address.
- Use the SDK's TBYB to protect against a buggy firmware update, and makes downgrade easier. The rom_explicit_buy() call automatically invalidates the other image if it is a downgrade, so bootrom uses the downgrade instead of the newest version.
- Upgrade frame has an extra CRC protecting the header so that accidental magic + arbitrary image size do not lock up a desynced reader for too long. Plus cap firmware images to 4MB.
- Update hardware key has the chip+architecture (RP2350_ARM), flash class requirement (2MB), pin layout (standard), and payload type (firmware). e.g. `RP2350_ARM-2MB-standard-firmware` is 32 bytes, so 64B would give plenty of headroom and is rare enough that wasted padding isn't important. Future schemes can use this for more nuance.


## Reference facts

Figures from the datasheets, ESP-IDF, and the pico-sdk 2.3.1 headers.

### ESP32 sender limits

UART limit is 5 Mbaud.
UART rates exact at 80 MHz: 2, 2.5, 4, 5 Mbaud. 3 Mbaud is 2.998 actual, 0.06% off.
SPI rates: 80, 40, 26.7, 20, 16, 13.3, 10 MHz.
SPI on native VSPI pins good for 80MHz. S3 GPIO matrix good for 40MHz.

### RP2354 receiver limits

PL011 UART cap 9.375 Mbaud (clk_peri / 16). Divisor has 6 fraction bits. 2, 3, 4, 5, 6, 8 Mbaud are exact.
PIO UART with 8 clocks per bit = 18.75 Mbaud.
PL022 SPI caps at 12.5 MHz SCK.
PIO SPI receive ~30-37 MHz SCK, about 20MHz with timeout code and sync clocks, plenty.

### UART drift and auto baud

Each byte resyncs on its own start bit edge, so the clocks only have to agree over 10 bits. Combined sender plus receiver error allowed is about 4% with 16x oversampling and about 3.5% with 8x.

Auto baud: the magic starts with 'U' (0x55) intentionally.

### SPI receive design points

- SPI sender will have periodic pauses in TX so clock/frames can realign on a timeout.
- The PL022 will not shift without SSn low. GP17 is unused on the board and can be SPI0 CSn, force the input low with INOVER in its GPIO CTRL register. The pad stays unconnected. Only needed on the PL022 path.
- PL022 doesn't have a good way to detect misalignment. Busy flag always set when CS, not just during mid-byte RX.
- Use PIO, no CS tricks, and can do timeout in PIO.

### Output with PIO, no transpose

 With PIO each channel has its own state machine and DMA channel fed from its own byte buffer. The CPU does not need to transpose bits.

 RP2350 has 3 PIO with 4 state machines each. Only need 8 state machines for output, optionally 1 for input if using PIO there. PIO has 32 instruction slots per block, WS2812 program is 4, APA102 about 4, both fit in one block.


### Memory

RP2350 has enough ram to double buffer every channel, so bad CRC does not have to erase inside a shared buffer, just keep the last frame's buffer. And overdraw can't write into a frame that is being drawn.

For bad crc, old expander would zero the data and disable the ws2812 start bits for that channel (so the output was held low). WS2812 LEDs latch and keep last frame. But not all ws2812 compatible LEDs like getting no data for a long time, so sending last valid frame is better.

### Bus math

Output bandwidth:
* One board is 8 channels at 800 kbps (ws2812) = 6.4 Mbps.

Input bandwidth:
* UART 2 Mbaud, 10 bits per byte = 1.6 Mbps payload (v1-3 expanders).
* 5Mbaud gets 4 Mbps. 
* 12.5MHz SPI is 7.8X, about 2 saturated boards.
* PIO SPI at 20 MHz is just over 3 saturated boards.
