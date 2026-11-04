cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Board header lives in boards/. The extension reads PICO_PLATFORM from the
# cache to pick the OpenOCD target and SVD file.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pixelblaze_output_expander CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(outputexpander C CXX ASM)

pico_sdk_init()

set(PBX_VERSION_MAJOR 4)
set(PBX_VERSION_MINOR 0)

set(PBX_SOURCES
    src/main.c
    src/input.c
    src/autobaud.c
    src/output.c
    src/leds.c
    src/loopback.c
    src/parser.c
    src/channels.c
    src/crc32.c
    src/gamma.cpp
    src/sintable.c
    src/imagedef.c
    src/update.c
)

# The SDK's uf2 would be the bare firmware at the start of flash, replacing
# the partition table. outputexpander.uf2 is built from the base image
# below instead.
set(PICO_NO_UF2 1)

# Two executables from the same sources, differing only in the TBYB flag of
# the IMAGE_DEF. The bootrom skips a TBYB image on a normal boot and runs it
# only after a flash update reboot, so:
#   outputexpander        boots normally. SWD loads, the debugger, and the
#                         base image.
#   outputexpander_update TBYB. The update image, the payload for the UPDATE
#                         record. Runs once after the update and falls back
#                         to the old slot if it never buys.
function(pbx_add_image TARGET)
    add_executable(${TARGET} ${PBX_SOURCES})

    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/src/outputexpander.pio
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_pio)

    pico_set_program_name(${TARGET} "Pixelblaze Output Expander")
    pico_set_program_version(${TARGET} "${PBX_VERSION_MAJOR}.${PBX_VERSION_MINOR}.0")

    # Version item in the IMAGE_DEF, which the bootrom compares between
    # slots, and a SHA-256 it verifies before booting the image.
    pico_set_binary_version(${TARGET} MAJOR ${PBX_VERSION_MAJOR} MINOR ${PBX_VERSION_MINOR})
    pico_hash_binary(${TARGET})

    pico_enable_stdio_uart(${TARGET} 0)
    pico_enable_stdio_usb(${TARGET} 0)

    # Core 1 never runs, so flash_safe_execute only needs interrupts off.
    target_compile_definitions(${TARGET} PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)

    target_link_libraries(${TARGET}
        pico_stdlib
        pico_bootrom
        pico_flash
        hardware_pio
        hardware_dma
        hardware_pwm
        hardware_uart
        hardware_clocks
        hardware_flash
    )

    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
    )

    pico_add_extra_outputs(${TARGET})

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/check_magic.cmake
        VERBATIM)
endfunction()

pbx_add_image(outputexpander)
pbx_add_image(outputexpander_update)
target_compile_definitions(outputexpander_update PRIVATE PICO_CRT0_IMAGE_TYPE_TBYB=1)

# Base image: the A/B partition table in sector 0, padded to 4 KB, then the
# plain firmware in slot A, padded with 0xff through slot B's first sector
# (0x61000) so the bootrom has exactly one bootable image. One write at
# 0x10000000, for first time programming, recovery, and manual updates.
# See flash_ab.sh.
#
# outputexpander.uf2 is the same image with the absolute family, so a
# BOOTSEL drop writes it at the start of flash whether or not a partition
# table is already there.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/pt.bin
    COMMAND picotool partition create --quiet ${CMAKE_CURRENT_LIST_DIR}/pt.json ${CMAKE_BINARY_DIR}/pt.bin
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/pt.json
    VERBATIM)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/outputexpander_base.bin
    COMMAND ${CMAKE_OBJCOPY} -I binary -O binary --gap-fill 0xff --pad-to 0x1000 pt.bin pt_padded.bin
    COMMAND ${CMAKE_COMMAND} -E cat pt_padded.bin outputexpander.bin > base_unpadded.bin
    COMMAND ${CMAKE_OBJCOPY} -I binary -O binary --gap-fill 0xff --pad-to 0x62000 base_unpadded.bin outputexpander_base.bin
    DEPENDS ${CMAKE_BINARY_DIR}/pt.bin outputexpander
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/outputexpander.uf2
    COMMAND picotool uf2 convert --quiet outputexpander_base.bin outputexpander.uf2 -o 0x10000000 --family absolute
    DEPENDS ${CMAKE_BINARY_DIR}/outputexpander_base.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM)
add_custom_target(base_image ALL DEPENDS ${CMAKE_BINARY_DIR}/outputexpander_base.bin ${CMAKE_BINARY_DIR}/outputexpander.uf2)
