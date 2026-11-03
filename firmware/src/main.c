#include "pico/stdlib.h"
#include "app.h"
#include "channels.h"
#include "input.h"
#include "output.h"
#include "leds.h"
#include "loopback.h"
#include "update.h"

static uint8_t busId;

uint32_t port_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

uint8_t port_bus_id(void) {
    return busId;
}

void port_draw_all(void) {
    output_draw();
}

// Three cuttable jumpers give bits 3 to 5 of the frame channel. Pulled up,
// so an intact jumper to ground reads 0 and a cut one reads 1.
static uint8_t read_bus_id(void) {
    const uint pins[3] = { PBX_ADDR1_PIN, PBX_ADDR2_PIN, PBX_ADDR3_PIN };
    for (int i = 0; i < 3; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
    sleep_us(100);
    uint8_t id = 0;
    for (int i = 0; i < 3; i++)
        if (gpio_get(pins[i]))
            id |= 1u << i;
    return id;
}

int main() {
    busId = read_bus_id();
    stats.busId = busId;

    leds_init();
    channels_init();
    output_init();
    input_init();
    loopback_init();
    update_init();

    for (;;) {
        update_feed_watchdog();
        input_poll();
        output_busy();
        leds_poll();
        loopback_poll();
    }
}
