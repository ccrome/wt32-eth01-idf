#include "config.h"
#include "server.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "relay.h"
#include "esp_spiffs.h"
#include "dirent.h"
#include "sensors.h"

#define TAG "server.c"
// Global state variables


void list_spiffs_files(void) {
    ESP_LOGE(TAG, "*****************************");
    DIR *dir = opendir("/www");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "File: ---%s---", entry->d_name);
    }

    closedir(dir);
    const char *fn = "www/index.html";
    FILE *fd = fopen(fn, "r");
    if (!fd) {
	ESP_LOGE(TAG, "COULD NOT OPEN %s", fn);
    }
    ESP_LOGE(TAG, "*****************************");
}


esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[128]; // Increased buffer size slightly for safety

    ESP_LOGI(TAG, "req->uri: %s", req->uri);
    snprintf(filepath, sizeof(filepath), "/www%.*s", (int)(sizeof(filepath) - 6), req->uri);
    ESP_LOGI(TAG, "LOOKING FOR FILE NAME: ---%s---", filepath);

    FILE *file = fopen(filepath, "r");
    if (!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buffer[256];
    size_t bytes_read;
    httpd_resp_set_type(req, "text/html");

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(file);
    return ESP_OK;
}

// Relay Toggle Handler
esp_err_t relay_post_handler(httpd_req_t *req) {
    char query[100];
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    // Validate Query Length
    if (query_len > 1) {
        if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
            char param_id[10];
            char param_state[10];

            // Parse 'id' and 'state' parameters
            if (httpd_query_key_value(query, "id", param_id, sizeof(param_id)) == ESP_OK &&
                httpd_query_key_value(query, "state", param_state, sizeof(param_state)) == ESP_OK) {
                
                int relay_id = atoi(param_id); // Convert ID to integer
		int state;
		if (strcmp(param_state, "on") == 0) {
		    state = 1;
		} if (strcmp(param_state, "off") == 0) {
		    state = 0;
		} if (strcmp(param_state, "toggle") == 0) {
		    state = 2;
		}

		bool relay_state = relay_get(relay_id); // Verify state after setting
		if (state == 2)
		    state = ! relay_state;
		
		// Set Relay State
		relay_set(relay_id, state);
		relay_state = relay_get(relay_id); // Verify state after setting
		httpd_resp_set_type(req, "text/json");
		char resp[75];
		char *err;
		if (relay_state == state) {
		    err = "";
		} else {
		    err = "Failed to set Relay";
		}
		snprintf(resp, sizeof(resp), "{\"relay\": %d, \"state\": %d, \"error\": \"%s\"}", relay_id, state, err);
		httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
		// ESP_LOGI(TAG, resp);
                return ESP_OK;
            }
        }
    }

    // Bad Request Response
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "Invalid parameters. Usage: /relay?id=<id>&state=on|off", HTTPD_RESP_USE_STRLEN);
    ESP_LOGE(TAG, "Invalid relay parameters");
    return ESP_FAIL;
}

// Temperature Data Handler
esp_err_t temperature_get_handler(httpd_req_t *req) {
    char resp[50];
    snprintf(resp, sizeof(resp), "{\"sensors\": [%.1f, %.1f]}", sensors_get(0), sensors_get(1));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// URI Handlers
httpd_uri_t index_uri = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = file_get_handler,
};

httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = file_get_handler
};

httpd_uri_t relay_uri = {
    .uri = "/relay",
    .method = HTTP_POST,
    .handler = relay_post_handler
};

httpd_uri_t temperature_uri = {
    .uri = "/sensors",
    .method = HTTP_GET,
    .handler = temperature_get_handler
};

// Start HTTP Server
httpd_handle_t start_webserver(void) {
    list_spiffs_files();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &relay_uri);
        httpd_register_uri_handler(server, &temperature_uri);
        httpd_register_uri_handler(server, &index_uri);
    }
    return server;
}

/* Event handler for Ethernet */
void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Connected");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Disconnected");
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

/* IP event handler */
void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    start_webserver();
}
