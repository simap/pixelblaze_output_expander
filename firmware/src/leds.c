// Status and draw LEDs on 16 bit PWM.
// Status: solid while data arrived in the last second, else breathing.
// Draw: on while both a valid channel frame and a draw happened in the last
// second.

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "leds.h"
#include "app.h"
#include "sintable.h"

static uint32_t lastMs;

static void pwm_pin_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 0xffff);
    pwm_set_gpio_level(pin, 0);
    pwm_set_enabled(slice, true);
}

void leds_init(void) {
    pwm_pin_init(PBX_STATUS_LED_PIN);
    pwm_pin_init(PBX_DRAW_LED_PIN);
}

void leds_poll(void) {
    uint32_t ms = port_millis();
    if (ms == lastMs)
        return;
    lastMs = ms;

    if (ms - stats.lastDataMs < 1000)
        pwm_set_gpio_level(PBX_STATUS_LED_PIN, PBX_STATUS_LED_BRIGHTNESS);
    else
        pwm_set_gpio_level(PBX_STATUS_LED_PIN, (PBX_STATUS_LED_BRIGHTNESS * sinTable[(ms >> 3) & 0x3f]) >> 8);

    if (ms - stats.lastValidChannelMs < 1000 && ms - stats.lastDrawMs < 1000)
        pwm_set_gpio_level(PBX_DRAW_LED_PIN, PBX_DRAW_LED_BRIGHTNESS);
    else
        pwm_set_gpio_level(PBX_DRAW_LED_PIN, 0);
}
