# Firmware update plan

Design background is in DESIGN_NOTES.md, Firmware update section.

## Features

- New record type carries a whole update payload in one UPXL frame with its CRC32. Broadcast, no board address. Not subject to the per channel byte limit.
- The UPXL frame header is unchanged. The update record starts with its own header: key, payload length, CRC32 of those fields. The parser checks that CRC before committing to a body it cannot rewind. A false sync on a magic sequence inside pixel data or an update payload otherwise skips a fake multi-MB body and eats real frames. Other record types keep their existing bound of about 16 KB per false sync. A hard cap on the length field, 8 MB, bounds the damage from a false sync that passes the CRC.
- The key is a 64-byte string constant in the board header, compared up to the first NUL. The running firmware accepts only its own key, so official boards and dev board variants each form a closed chain. No OTP. Other payload types later use other keys with the same record.
- The pixel buffer pool and the update region are one union, 384 KB, one flash slot. The current firmware is about 37 KB.
- Point of no return is header CRC pass, key match, and length within the region. The output DMA channels are aborted, the LEDs hold, and the body streams into the region from offset 0. No body byte has been copied before this point, so nothing is moved. After this point every exit is a reboot.
- A header with another key, or a length over the RAM limit, skips the body without buffering. The CRC still streams through the sniffer, so a skipped frame counts as valid. Frames for other boards can be many MB and take minutes at low rates, so only the inter-byte idle timeout applies, never a per-frame one.
- Frame CRC32 is the only check before erase. The firmware is sealed with a SHA-256 at build time and the bootrom verifies it at the flash update boot. A damaged payload that passed CRC32 is refused there and the old slot boots.
- Install is exact version. Downgrades are allowed. The only skip is a HASH_VALUE in the buffered IMAGE_DEF equal to the running image's, which means the same image. A repair of a limping board still goes through and dev builds do not need a version bump.
- A/B slots selected by the bootrom. The update writes the inactive slot only. Power loss during erase or program leaves the old image bootable.
- Erase and program from the RAM buffer with interrupts off. No pacing needed because the frame has already ended.
- Activation always goes through a flash update boot, which runs the written slot once. On its first frame with a good CRC the new image buys itself if the boot has a buy pending. An update record for this board arriving unbought buys at its header and is then refused, so an unbought image never overwrites its fallback. The header CRC is proof enough that the input works, and the buy laps the ring so the rest of that frame is lost anyway. The sender resends. The buy erases the other slot's first sector, so the bought image is preferred from then on whatever the versions (datasheet 5.1.17). A non TBYB image launched by a flash update boot gets the same erase from the bootrom at launch.
- The bootrom launches an unbought TBYB image under its own 16.7 s watchdog. The main loop reloads it, so the buy has no time limit while the firmware runs. A hang stops the reloads and the bootrom reboots into the old slot.
- Every build produces two images. The update image, outputexpander_update.bin, is the app firmware alone with the TBYB flag, the UPDATE payload for embedding in another microcontroller. The base image, outputexpander_base.bin and outputexpander.uf2, is the partition table plus the plain firmware, for first time programming, recovery, and manual updates. It is not TBYB because a TBYB image boots only through a flash update reboot and a plain SWD or BOOTSEL load will not run it. Development builds go in over SWD, where flash_ab.sh controls the slots directly.
- The buy costs a stall of up to a few hundred ms with interrupts off, once per update.
- Any validation failure before flash reboots back to the running firmware. No flash is touched.

## TODO

1. Partition table JSON: one A/B pair, A at 4K and B at 388K, 384 KB each. The rest of flash is left for a future filesystem. The build packs the table, padded to 4 KB, and the sealed firmware into outputexpander_base.bin, which is what production boards get at 0x10000000. flash_ab.sh writes it over SWD.
2. CMake: pico_set_binary_version with MAJOR and MINOR, pico_hash_binary, PICO_CRT0_IMAGE_TYPE_TBYB=1. Confirm the .bin from pico_add_extra_outputs is produced from the sealed ELF. Confirm picotool load still boots a TBYB image during development.
3. Hardware key constant per board variant in the board header. Add a dev board variant that moves the input pins.
4. Protocol: PB_FIRMWARE record type and header struct with header CRC32 in protocol.h. Pixelblaze side sender in a separate task.
4a. Build check: fail the build if the sealed .bin contains the magic sequence. The compiler can emit the 32-bit magic as a literal pool word, so a source-level split does not guarantee this.
5. Parser: on a matching header, abort output DMA and stream the body into the update region over the pool. Timeout or bad CRC reboots. On any other header, discard the body through the CRC path with no destination buffer. Bad header CRC resumes the magic search at the next byte.
6. Skip check: block loop walk in the buffer to find the HASH_VALUE item. Equal to the running image's HASH_VALUE skips the install. No hash computation on the board.
7. Slot lookup: rom_get_boot_info for the running partition number. The slot offsets and size are constants in the board header, matching pt.json.
8. Flash write: flash_range_erase and flash_range_program over the slot. Core 1 parked, interrupts off, nothing fetching from flash. Pet the watchdog between sectors.
9. Reboot: rom_reboot with REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE and the slot address as p0.
10. Buy: at boot read boot_info. If BUY_PENDING, buy on the first CRC-valid frame of any type, updates for other keys included, after that frame is dispatched, or at the header of an update for this board. The work area is the receive target, which is empty at both points. No time limit on the buy. The main loop reloads the bootrom's TBYB watchdog. Its reset is a normal boot, which does not pick an unbought image over the old one.
11. Stats: update state, bytes received, validation result, last error, readable over SWD.
12. Tests: wrong key ignored, bad CRC rejected, magic inside a frame body does not desync, power on mid firmware frame resyncs on the next frame, bad hash refused by the bootrom and old slot boots, same hash skipped, downgrade installs and boots, power cut mid program boots old slot, TBYB image that never buys boots old slot.
13. README: update procedure.

## Status

Items 1 to 11 and 13 are implemented. Item 12 has host tests for the parser and block walk. The flash and reboot path has not been run on hardware yet. flash_ab.sh writes the base image over SWD. Item 4's Pixelblaze side sender is not started.
