// Pixelblaze Output Expander, RP2354A.
// SYSINFO PACKAGE_SEL reads 1 on the board (QFN-60), so GPIO0 to GPIO29 exist.
// Flash is the 2 MB die inside the package (W25Q16JV, read by OpenOCD).

#ifndef _BOARDS_PIXELBLAZE_OUTPUT_EXPANDER_H
#define _BOARDS_PIXELBLAZE_OUTPUT_EXPANDER_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PIXELBLAZE_OUTPUT_EXPANDER

#define PICO_RP2350A 1

// LED data outputs, one per channel, consecutive for PIO out/side-set.
// Channel numbering is reversed: GP0 is channel 7, GP7 is channel 0.
#define PBX_LED_OUT_PIN_BASE 0
#define PBX_LED_OUT_COUNT 8
#define PBX_LED_OUT_PIN(ch) (PBX_LED_OUT_PIN_BASE + 7 - (ch))

// Address jumpers. Jumper 1 is the low bit.
#define PBX_ADDR1_PIN 15
#define PBX_ADDR2_PIN 14
#define PBX_ADDR3_PIN 13

// Input data net is on GP19 and GP20. GP19 is UART0 RX on the aux function
// (GPIO_FUNC_UART_AUX). GP20 and GP22 feed the PIO SPI receiver, which
// needs the clock 2 GPIOs above the data pin.
#define PBX_DATA_IN_UART_PIN 19
#define PBX_DATA_IN_SPI_PIN 20
#define PBX_CLK_IN_PIN 22

// Active high.
#define PBX_STATUS_LED_PIN 21
#define PBX_DRAW_LED_PIN 23

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN PBX_DATA_IN_UART_PIN
#endif

#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN PBX_CLK_IN_PIN
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN PBX_DATA_IN_SPI_PIN
#endif

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN PBX_STATUS_LED_PIN
#endif

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
