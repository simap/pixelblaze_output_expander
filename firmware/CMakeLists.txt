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
)

pico_generate_pio_header(outputexpander ${CMAKE_CURRENT_LIST_DIR}/src/outputexpander.pio)

pico_set_program_name(outputexpander "Pixelblaze Output Expander")
pico_set_program_version(outputexpander "4.0.0")

pico_enable_stdio_uart(outputexpander 0)
pico_enable_stdio_usb(outputexpander 0)

target_link_libraries(outputexpander
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_uart
    hardware_clocks
)

target_include_directories(outputexpander PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_add_extra_outputs(outputexpander)
