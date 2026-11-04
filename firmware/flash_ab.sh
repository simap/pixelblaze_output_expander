#!/bin/zsh
# Base image flash over SWD: outputexpander_base.bin at the start of flash,
# which is the A/B partition table in sector 0, the firmware in slot A, and
# slot B's first sector cleared so the bootrom has exactly one bootable
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

"$OCD_DIR/openocd" -s "$OCD_DIR/scripts" \
    -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed 5000" \
    -c "init" \
    -c "reset halt" \
    -c "program $BUILD/outputexpander_base.bin 0x10000000 verify reset" \
    -c "shutdown"
