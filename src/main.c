#include <string.h>
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "ethernet.h"
#include "esp_spiffs.h"

#include "relay.h"
#include "server.h"
#include "sensors.h"

#define TAG "MAIN APP"

void init_fs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS successfully mounted");
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS Total: %d, Used: %d", total, used);
    } else {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition info (%s)", esp_err_to_name(ret));
    }

    ret = esp_spiffs_check(conf.partition_label);
    ESP_LOGE(TAG, "-----------------------");
    switch (ret) {
    case ESP_OK:
        ESP_LOGE(TAG, "check ok!");
	break;
    case ESP_ERR_INVALID_STATE:
        ESP_LOGE(TAG, "not mounted!");
	break;
    case ESP_FAIL:
    default:
        ESP_LOGE(TAG, "ANOTHER ERROR!");
	break;
    }
}

/* App Main */
void app_main(void) {
    relay_init();
    init_fs();
    sensors_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register Ethernet and IP Event Handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, NULL));

    // Initialize Ethernet
    init_ethernet();
    TaskHandle_t sensor_task;
    xTaskCreate(
        SensorTask,    // Task function
        "Sensors",      // Name of the task
        2000,         // Stack size
        NULL,             // Task parameters
        1,                // Priority
        &sensor_task           // Task handle (not used here)
    );
    // Keep FreeRTOS Task Running
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
//	ESP_LOGE(TAG, "Stack high water mark = %ld words, %ld bytes",
//		 (long int)(uxTaskGetStackHighWaterMark(sensor_task)),
//		 (long int)(uxTaskGetStackHighWaterMark(sensor_task)*4));
    }
}
