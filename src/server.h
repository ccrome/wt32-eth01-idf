#ifndef __SERVER_H__
#define __SERVER_H__
#include "esp_http_server.h"

void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);
void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
#endif
