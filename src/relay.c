#include <stdint.h>
#include "driver/gpio.h"
#include "relay.h"

uint8_t relay_pins[] = {4, 14};
uint8_t relay_state[] = {0, 0};
bool relay_polarity[] = {1, 1};

#define N_RELAYS (sizeof(relay_pins)/sizeof(relay_pins[0]))

int get_n_relays() {
    return N_RELAYS;
}

void relay_init(void)
{
    for (int i = 0; i < N_RELAYS; i++) {
	gpio_config_t io_conf = {
	    .pin_bit_mask = (1ULL << relay_pins[i]), // Pin mask for GPIO16
	    .mode = GPIO_MODE_OUTPUT,          // Set as output
	    .pull_down_en = GPIO_PULLDOWN_DISABLE,
	    .pull_up_en = GPIO_PULLUP_ENABLE,
	    .intr_type = GPIO_INTR_DISABLE,    // No interrupts
	};
	gpio_set_level(relay_pins[i], relay_polarity[i]);
	gpio_config(&io_conf);
    }
}

void relay_set(uint8_t relay_id, bool state)
{
    if (relay_id >= N_RELAYS)
	return;
    bool polarity = relay_polarity[relay_id];
    bool gpio_state = state ^ polarity;
    gpio_set_level(relay_pins[relay_id], gpio_state);
    relay_state[relay_id] = state;
}

void relay_on(uint8_t relay_id) {
    relay_set(relay_id, 1);
}

void relay_off(uint8_t relay_id) {
    relay_set(relay_id, 0);
}

void relay_toggle(uint8_t relay_id) {
    if (relay_id >= N_RELAYS)
	return;
    relay_state[relay_id] = !relay_state[relay_id];
    gpio_set_level(relay_pins[relay_id], relay_state[relay_id]);
}

uint8_t relay_get(uint8_t relay_id) {
    if (relay_id >= N_RELAYS)
	return 255;
    return relay_state[relay_id];
}
