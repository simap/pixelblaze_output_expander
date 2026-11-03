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

# A TBYB image boots only through a flash update reboot, so a plain SWD load
# and reset will not run it. Leave off for development, on for releases.
option(PBX_TBYB "Build a try-before-you-buy image" OFF)

add_executable(outputexpander
    src/main.c
    src/input.c
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

pico_generate_pio_header(outputexpander ${CMAKE_CURRENT_LIST_DIR}/src/outputexpander.pio)

pico_set_program_name(outputexpander "Pixelblaze Output Expander")
pico_set_program_version(outputexpander "${PBX_VERSION_MAJOR}.${PBX_VERSION_MINOR}.0")

# Version item in the IMAGE_DEF, which the bootrom compares between slots,
# and a SHA-256 it verifies before booting the image.
pico_set_binary_version(outputexpander MAJOR ${PBX_VERSION_MAJOR} MINOR ${PBX_VERSION_MINOR})
pico_hash_binary(outputexpander)

pico_enable_stdio_uart(outputexpander 0)
pico_enable_stdio_usb(outputexpander 0)

# Core 1 never runs, so flash_safe_execute only needs interrupts off.
target_compile_definitions(outputexpander PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)
if (PBX_TBYB)
    target_compile_definitions(outputexpander PRIVATE PICO_CRT0_IMAGE_TYPE_TBYB=1)
endif()

target_link_libraries(outputexpander
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

target_include_directories(outputexpander PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_add_extra_outputs(outputexpander)

add_custom_command(TARGET outputexpander POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DBIN=$<TARGET_FILE_DIR:outputexpander>/outputexpander.bin
        -P ${CMAKE_CURRENT_LIST_DIR}/cmake/check_magic.cmake
    VERBATIM)

# Factory image: the A/B partition table in sector 0, padded to 4 KB, then
# the sealed firmware in slot A. One write at 0x10000000. See flash_ab.sh.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/pt.bin
    COMMAND picotool partition create --quiet ${CMAKE_CURRENT_LIST_DIR}/pt.json ${CMAKE_BINARY_DIR}/pt.bin
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/pt.json
    VERBATIM)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/outputexpander_factory.bin
    COMMAND ${CMAKE_OBJCOPY} -I binary -O binary --gap-fill 0xff --pad-to 0x1000 pt.bin pt_padded.bin
    COMMAND ${CMAKE_COMMAND} -E cat pt_padded.bin outputexpander.bin > outputexpander_factory.bin
    DEPENDS ${CMAKE_BINARY_DIR}/pt.bin outputexpander
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_target(factory_image ALL DEPENDS ${CMAKE_BINARY_DIR}/outputexpander_factory.bin)
