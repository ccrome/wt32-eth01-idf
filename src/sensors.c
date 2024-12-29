#include "sensors.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"
#include <string.h>
#include <math.h>


#define GPIO_DS18B20_0 15
#define MAX_DEVICES 2
#define TAG "SENSORS"

typedef enum {
    CONVERTER_IDLE,
    CONVERTER_CONVERTING,
    CONVERTER_READY
} SensorConversionState_t;


typedef struct {
    OneWireBus * owb;
    OneWireBus_ROMCode device_rom_codes[MAX_DEVICES];
    int num_devices;
    DS18B20_Info * devices[MAX_DEVICES];
    int errors_count[MAX_DEVICES];
    DS18B20_ERROR errors[MAX_DEVICES];
    float readings[MAX_DEVICES];
    int sample_count;
    owb_rmt_driver_info rmt_driver_info;
    SensorConversionState_t conversion_state;
    StaticSemaphore_t lock_buffer;
    SemaphoreHandle_t lock;

} Sensors_t;

static Sensors_t _sensors;


void sensors_init() {
    memset(&_sensors, 0, sizeof(_sensors));
    _sensors.lock = xSemaphoreCreateMutexStatic(&_sensors.lock_buffer);
    if (xSemaphoreTake(_sensors.lock, pdMS_TO_TICKS(10)) != pdTRUE) {
	ESP_LOGE(TAG, "Cocouldn't take mutex!  this is seriously wrong! %s:%d", __FILE__, __LINE__);
	return;
    }
    _sensors.conversion_state = CONVERTER_IDLE;
    _sensors.owb = owb_rmt_initialize(&_sensors.rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);
    owb_use_crc(_sensors.owb, true);  // enable CRC check for ROM code
    ESP_LOGE(TAG, "Find devices:");
    OneWireBus_SearchState search_state = {0};
    bool found = false;
    owb_search_first(_sensors.owb, &search_state, &found);
    while (found)
    {
        char rom_code_s[17];
        owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
        ESP_LOGE(TAG, "  %d : %s", _sensors.num_devices, rom_code_s);
        _sensors.device_rom_codes[_sensors.num_devices] = search_state.rom_code;

	DS18B20_Info * ds18b20_info = ds18b20_malloc();  // heap allocation
	assert(ds18b20_info);
        _sensors.devices[_sensors.num_devices] = ds18b20_info;
	ds18b20_init(ds18b20_info, _sensors.owb, _sensors.device_rom_codes[_sensors.num_devices]); // associate with bus and device
	ds18b20_use_crc(ds18b20_info, true);
	ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION_12_BIT);
	++_sensors.num_devices;
        owb_search_next(_sensors.owb, &search_state, &found);
    }
    ESP_LOGE(TAG, "Found %d device%s\n", _sensors.num_devices, _sensors.num_devices == 1 ? "" : "s");

    OneWireBus_ROMCode rom_code;
    owb_status status = owb_read_rom(_sensors.owb, &rom_code);
    if (status == OWB_STATUS_OK)
    {
	char rom_code_s[OWB_ROM_CODE_STRING_LENGTH];
	owb_string_from_rom_code(rom_code, rom_code_s, sizeof(rom_code_s));
	ESP_LOGE(TAG, "Single device %s present\n", rom_code_s);
    }
    else
    {
	ESP_LOGE(TAG, "An error occurred reading ROM code: %d", status);
    }
    bool parasitic_power = false;
    ds18b20_check_for_parasite_power(_sensors.owb, &parasitic_power);
    if (parasitic_power) {
        ESP_LOGE(TAG, "Parasitic-powered devices detected");
    } else {
        ESP_LOGE(TAG, "NO Parasitic-powered devices detected");
    }

    ds18b20_convert_all(_sensors.owb);
    _sensors.conversion_state = CONVERTER_CONVERTING;
    xSemaphoreGive(_sensors.lock);

}

float sensors_get(uint32_t sensor_id) {
    uint32_t timeout_ms = 10;
    float value = -1000;
    if (sensor_id < _sensors.num_devices) {
	if (xSemaphoreTake(_sensors.lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
	    value =  _sensors.readings[sensor_id];
	    xSemaphoreGive(_sensors.lock);
	} else {
	    ESP_LOGE(TAG, "Cocouldn't take mutex!  this is seriously wrong! %s:%d", __FILE__, __LINE__);
	}
    }
    return value;
}


void SensorTask(void *parameters) {
    (void)parameters; // Avoid unused parameter warning
    while (1) {
	if (_sensors.num_devices > 0) {
	    ds18b20_convert_all(_sensors.owb);
	    ds18b20_wait_for_conversion(_sensors.devices[0]);
	    for (int i = 0; i < _sensors.num_devices; ++i)
	    {
		float reading;
		_sensors.errors[i] = ds18b20_read_temp(_sensors.devices[i], &reading);
		ESP_LOGE(TAG, "temp: %.1f", reading);
		if (xSemaphoreTake(_sensors.lock, pdMS_TO_TICKS(100)) == pdTRUE) {
		    _sensors.readings[i] = reading;
		    xSemaphoreGive(_sensors.lock);
		} else {
		    ESP_LOGE(TAG, "Cocouldn't take mutex!  this is seriously wrong! %s:%d", __FILE__, __LINE__);
		}
		if (_sensors.errors[i] != DS18B20_OK)
		    ++_sensors.errors_count[i];
	    }
	} else {
	    vTaskDelay(1000);
	}
    }
}
