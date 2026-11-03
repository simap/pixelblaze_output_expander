# Firmware update plan

Design background is in DESIGN_NOTES.md, Firmware update section.

## Features

- New record type carries the whole firmware image in one UPXL frame with its CRC32. Broadcast, no board address. Not subject to the per channel byte limit.
- The UPXL frame header is unchanged. The firmware record payload starts with its own header: hardware key, image length, CRC32 of those fields. The parser checks that CRC before committing to a body it cannot rewind. A false sync on a magic sequence inside pixel data or a firmware image otherwise skips a fake multi-MB body and eats real frames. Other record types keep their existing bound of about 16 KB per false sync. A hard cap on the firmware length field, 8 MB, bounds the damage from a false sync that passes the CRC.
- A gap in the input longer than a few ms resets the parser to magic search. A real firmware frame has no gaps.
- Hardware key is a 64-byte string constant compiled into the image, compared up to the first NUL. The running firmware accepts only its own key, so official boards and dev board variants each form a closed chain. No OTP. Other payload types later use other keys with the same record.
- The pixel buffer pool is overlaid on the base of one large image region that covers all RAM not used by the rings, stacks, tables, and DMA control blocks, about 450 to 500 KB. The current image is about 32 KB.
- Point of no return is header CRC pass, key match, and length within the region. The output DMA channels are aborted, the LEDs hold, and the body streams into the region from offset 0. No body byte has been copied before this point, so nothing is moved. After this point every exit is a reboot.
- A header with another key, or a length over the RAM limit, skips the body without buffering. The CRC still streams through the sniffer, so a skipped frame counts as valid. Frames for other boards can be many MB and take minutes at low rates, so only the inter-byte idle timeout applies, never a per-frame one.
- Frame CRC32 is the only check before erase. The image is sealed with a SHA-256 at build time and the bootrom verifies it at the flash update boot. A damaged image that passed CRC32 is refused there and the old slot boots.
- Install is exact version. Downgrades are allowed. The only skip is a HASH_VALUE in the buffered IMAGE_DEF equal to the running image's, which means the same image. A repair of a limping board still goes through and dev builds do not need a version bump.
- A/B slots selected by the bootrom. The update writes the inactive slot only. Power loss during erase or program leaves the old image bootable.
- Erase and program from the RAM buffer with interrupts off. No pacing needed because the frame has already ended.
- Every image is built with the TBYB flag. Activation always goes through a flash update boot and a buy. The buy handles downgrades by erasing the other slot's first sector.
- The new image buys after the first valid frame has been parsed. A crash or watchdog before the buy boots the old slot.
- The buy runs only when boot info reports BUY_PENDING. It costs one dropped frame on that boot and nothing afterwards.
- Any validation failure before flash reboots back to the current image. No flash is touched.

## TODO

1. Partition table JSON: one A/B pair, each slot just under 1 MB. Embed with pico_embed_pt_in_binary on the factory image.
2. CMake: pico_set_binary_version with MAJOR and MINOR, pico_hash_binary, PICO_CRT0_IMAGE_TYPE_TBYB=1. Confirm the .bin from pico_add_extra_outputs is produced from the sealed ELF. Confirm picotool load still boots a TBYB image during development.
3. Hardware key constant per board variant in the board header. Add a dev board variant that moves the input pins.
4. Protocol: PB_FIRMWARE record type and header struct with header CRC32 in protocol.h. Pixelblaze side sender in a separate task.
4a. Build check: fail the build if the sealed .bin contains the magic sequence. The compiler can emit the 32-bit magic as a literal pool word, so a source-level split does not guarantee this.
5. Parser: on a matching header, abort output DMA and stream the body into the image region over the pool. Timeout or bad CRC reboots. On any other header, discard the body through the CRC path with no destination buffer. Bad header CRC resumes the magic search at the next byte. Input idle longer than a few ms resets to magic search.
6. Skip check: block loop walk in the buffer to find the HASH_VALUE item. Equal to the running image's HASH_VALUE skips the install. No hash computation on the board.
7. Slot lookup: rom_get_boot_info for the running partition, rom_get_partition_table_info for the other slot's flash range.
8. Flash write: flash_range_erase and flash_range_program over the slot. Core 1 parked, interrupts off, nothing fetching from flash. Pet the watchdog between sectors.
9. Reboot: rom_reboot with REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE and the slot address as p0.
10. Buy: at boot read boot_info. If BUY_PENDING, buy on the first CRC-valid frame of any type, firmware frames for other keys included, before that frame is dispatched. 4 KB aligned scratch buffer. No time limit on the buy. The watchdog runs always, 500 ms, petted from the main loop. A watchdog reset is a normal boot, which never boots an unbought image.
11. Stats: update state, bytes received, validation result, last error, readable over SWD.
12. Tests: wrong key ignored, bad CRC rejected, magic inside a frame body does not desync, power on mid firmware frame resyncs on the next frame, bad hash refused by the bootrom and old slot boots, same hash skipped, downgrade installs and boots, power cut mid program boots old slot, TBYB image that never buys boots old slot.
13. README: update procedure.
