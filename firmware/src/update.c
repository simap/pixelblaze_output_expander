#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "boot/picoboot_constants.h"
#include "update.h"
#include "imagedef.h"
#include "channels.h"
#include "output.h"
#include "app.h"

static int8_t bootPartition = -1;   // PBX_SLOT_A or PBX_SLOT_B, anything else refuses updates
static bool buyPending;

// Physical flash offset of the slot this image is not running from.
static uint32_t other_offset(void) {
    return bootPartition == PBX_SLOT_A ? PBX_SLOT_B_OFFSET : PBX_SLOT_A_OFFSET;
}

const char *port_update_key(void) {
    return PBX_UPDATE_KEY;
}

void update_feed_watchdog(void) {
    watchdog_hw->load = WATCHDOG_LOAD_BITS;
}

// Sector erase with interrupts off, one sector per call so the watchdog is
// fed between them. A sector erase on the W25Q16JV is 400 ms worst case.
static void erase_sectors(uint32_t offset, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        update_feed_watchdog();
        uint32_t save = save_and_disable_interrupts();
        flash_range_erase(offset + i * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        restore_interrupts(save);
    }
}

static void program_pages(uint32_t offset, const uint8_t *data, uint32_t len) {
    for (uint32_t done = 0; done < len; done += FLASH_SECTOR_SIZE) {
        uint32_t n = len - done < FLASH_SECTOR_SIZE ? len - done : FLASH_SECTOR_SIZE;
        update_feed_watchdog();
        uint32_t save = save_and_disable_interrupts();
        flash_range_program(offset + done, data + done, n);
        restore_interrupts(save);
    }
}

void update_init(void) {
    stats.bootPartition = -1;

    boot_info_t info;
    memset(&info, 0, sizeof(info));
    if (!rom_get_boot_info(&info))
        return;
    stats.bootType = info.boot_type;

    bootPartition = info.partition;
    stats.bootPartition = bootPartition;

    buyPending = info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING;
    stats.activation = buyPending ? PBX_ACTIVATION_PENDING : PBX_ACTIVATION_NONE;
}

// Buy a TBYB image. The bootrom clears the flag and erases the other slot's
// first sector so this one boots from now on. It reads, erases, and
// rewrites the sector holding the flag through a 4 KB word aligned work
// area, datasheet 5.4.8.4. 
//
// A failed buy leaves the flag sector in an unknown state, so this image
// may no longer boot. A normal boot right away runs it if the rewrite
// completed, or the other image if not and that one is intact.
static void buy(void) {
    buyPending = false;
    // Borrow the receive target buffer, it is empty between frames.
    uint32_t *work = channels_receive_target();
    stats.buyResult = rom_explicit_buy((uint8_t *) work, 4096);
    if (stats.buyResult < 0) {
        stats.activation = PBX_ACTIVATION_FAILED;
        rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);
    }
    stats.activation = PBX_ACTIVATION_DONE;
}

// Called after the frame is dispatched, when the receive target is fresh.
void port_frame_valid(void) {
    if (buyPending)
        buy();
}

uint8_t *port_update_begin(uint32_t len) {
    if (bootPartition != PBX_SLOT_A && bootPartition != PBX_SLOT_B) {
        stats.updateRefused = PBX_REFUSED_NO_SLOT;
        return NULL;
    }
    uint32_t size;
    uint8_t *region = channels_update_region(&size);
    _Static_assert(PBX_UPDATE_REGION_BYTES == PBX_SLOT_SIZE, "update region is one slot");
    if (len > size) {
        stats.updateRefused = PBX_REFUSED_TOO_BIG;
        return NULL;
    }
    // An unbought image must not overwrite its fallback. The header CRC
    // that got here is proof the input works, so buy now, while the pool
    // is intact. The buy laps the ring, so the rest of this frame is lost
    // anyway. It is consumed CRC only and the LEDs keep running.
    if (buyPending) {
        buy();
        stats.updateRefused = PBX_REFUSED_UNBOUGHT;
        return NULL;
    }
    output_abort();
    return region;
}

void port_update_abort(void) {
    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);
}

void port_update_apply(uint8_t *payload, uint32_t len) {
    stats.updateState = PBX_UPDATE_FLASHING;

    imagedef_t incoming, running;
    imagedef_read(payload, len, &incoming);
    if (!incoming.found)
        rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);
    // The running image is at XIP_BASE through the bootrom's rolling
    // window whichever slot it is in. Its loop may run to the slot's end.
    imagedef_read((const uint8_t *) XIP_BASE, PBX_SLOT_SIZE, &running);
    if (incoming.hasHash && running.hasHash && memcmp(incoming.hash, running.hash, IMAGEDEF_HASH_LEN) == 0)
        rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);

    uint32_t padded = (len + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    memset(payload + len, 0xff, padded - len);
    erase_sectors(other_offset(), (padded + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE);
    program_pages(other_offset(), payload, padded);

    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, XIP_BASE + other_offset(), 0);
}
