#!/bin/zsh
# Factory flash over SWD: outputexpander_factory.bin at the start of flash,
# which is the A/B partition table in sector 0 and the firmware in slot A.
# Slot B's first sector is erased so the bootrom has exactly one bootable
# image. After this, updates over the data input alternate between the
# slots.
#
# The VS Code extension's flash task and debug launch program the ELF's
# segments at their link addresses, from the start of flash, which replaces
# the partition table with a bare image. That still boots and runs, but
# updates are refused with updateRefused = PBX_REFUSED_NO_SLOT. Run this
# script to restore the layout.
#
# Usage: flash_ab.sh [build dir]
# Environment: PICO_SDK_OPENOCD (default ~/.pico-sdk/openocd/0.12.0+dev)

set -e
BUILD=${1:-$(dirname "$0")/build}
OCD_DIR=${PICO_SDK_OPENOCD:-$HOME/.pico-sdk/openocd/0.12.0+dev}

# Slot B offset from pt.json, also in boards/pixelblaze_output_expander.h.
SLOT_B=0x10061000

"$OCD_DIR/openocd" -s "$OCD_DIR/scripts" \
    -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed 5000" \
    -c "init" \
    -c "reset halt" \
    -c "flash erase_address $SLOT_B 0x1000" \
    -c "program $BUILD/outputexpander_factory.bin 0x10000000 verify reset" \
    -c "shutdown"
