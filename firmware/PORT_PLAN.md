# RP2354 port plan

Design background is in DESIGN_NOTES.md.

## Features

- Protocol unchanged: UPXL magic, 5 record types, CRC32, bus ID from jumpers, channel reversed (GP0 is channel 7).
- Input over UART at 2, 3, 4, or 5 Mbaud, or SPI with no chip select.
- Both receivers run always. SPI ring wins when live. UART ring served after an SPI quiet timeout.
- Auto baud from the 'U' magic byte.
- Per channel PIO state machine and DMA channel for output. No bit transpose.
- WS2812 and APA102 mixed across channels. APA102 pairs use data plus side-set clock.
- Each channel sends only its own length.
- Double buffered channels. Swap on CRC pass. Last valid frame redrawn on failure.
- Frames stream from the ring in chunks through a DMA copy with the sniffer computing CRC32, straight into the receive buffer. Software CRC as fallback.
- Status and draw LEDs with the existing timing rules.
- Volatile stats struct readable live over SWD.
- USB unused at this stage. Reserved for a future input or flashing.

## TODO

1. Board header and stats struct. Status LED PWM, draw LED PWM, jumpers read at boot.
2. UART0 RX on GP19 with DMA into an 8 KB aligned ring. Parser as a non blocking state machine over the ring.
3. CRC32: mem2mem DMA with sniffer (CRC32R, reversed and inverted output, seed 0xffffffff). Software table version alongside for comparison.
4. Pool of 17 pixel buffers. Draw pointer and candidate per channel, one receive target. Good CRC promotes the receive target to candidate.
5. WS2812 PIO program, 8 state machines across PIO0 and PIO1, one DMA channel each, sync start on DRAW_ALL, 300 us latch window by timestamp, overdraw counted and dropped.
6. Test against a Pixelblaze at 2 Mbaud on all 8 channels. First milestone.
7. APA102 data and clock records. PIO program with side-set clock. FAST_DATA path with gamma tables. Honour the frequency field per channel.
8. PIO SPI receiver on GP20 and GP22, no select line, 10 us idle timeout clears a partial byte, DMA into a second ring. Live threshold, quiet timeout, UART ring discard on switch.
9. UART rates 2, 3, 4, 5 Mbaud. Auto baud with a PIO UART receiver timing the 'U' byte.
10. PIO SPI receiver for clocks above 12.5 MHz.
11. Decide what the LEDs show on CRC errors.
12. README update for the new rates and SPI input.
