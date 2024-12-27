#ifndef _RELAY_H_
#define _RELAY_H_
#include <stdint.h>

void relay_init(void);
void relay_on(uint8_t relay_id);
void relay_off(uint8_t relay_id);
void relay_set(uint8_t relay_id, bool state);
void relay_toggle(uint8_t relay_id);
uint8_t relay_get(uint8_t relay_id);
int get_n_relays();
#endif

