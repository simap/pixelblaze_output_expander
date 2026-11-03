// Firmware update over the data input. See FIRMWARE_UPDATE_PLAN.md.
//
// The bootrom owns the A/B slot choice. This module writes a received
// payload into the other slot and reboots with a flash update boot, which runs the
// new slot once. A TBYB image then buys itself on its first frame with a
// good CRC. The bootrom launches an unbought TBYB image under a 16.7 s
// watchdog, datasheet 5.1.17. A hang stops the reloads and the bootrom
// falls back to the old image.

#ifndef UPDATE_H
#define UPDATE_H

// Reads boot info and the partition table. Call once before the main loop.
void update_init(void);

// Reloads the bootrom's TBYB watchdog so the buy has no time limit while
// the main loop runs. Harmless when no watchdog is armed.
void update_feed_watchdog(void);

#endif
