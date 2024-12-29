#include <string.h>
#include "nanomodbus.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_netif.h"


#define MODBUS_PORT 502

#define TAG "modbus.c"


static int32_t modbus_read(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg);
static int32_t modbus_write(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg);
static nmbs_error read_coils(uint16_t address, uint16_t quantity, nmbs_bitfield coils_out, uint8_t unit_id, void* arg);
static nmbs_error read_discrete_inputs(uint16_t address, uint16_t quantity, nmbs_bitfield inputs_out, uint8_t unit_id, void* arg);
static nmbs_error read_holding_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg);
static nmbs_error read_input_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg);
static nmbs_error write_single_coil(uint16_t address, bool value, uint8_t unit_id, void* arg);
static nmbs_error write_single_register(uint16_t address, uint16_t value, uint8_t unit_id, void* arg);
static nmbs_error write_multiple_coils(uint16_t address, uint16_t quantity, const nmbs_bitfield coils, uint8_t unit_id, void* arg);
static nmbs_error write_multiple_registers(uint16_t address, uint16_t quantity, const uint16_t* registers, uint8_t unit_id, void* arg);
static nmbs_error read_file_record(uint16_t file_number, uint16_t record_number, uint16_t* registers, uint16_t count, uint8_t unit_id, void* arg);
static nmbs_error write_file_record(uint16_t file_number, uint16_t record_number, const uint16_t* registers, uint16_t count, uint8_t unit_id, void* arg);
static nmbs_error read_device_identification(uint8_t object_id, char buffer[NMBS_DEVICE_IDENTIFICATION_STRING_LENGTH]);
static nmbs_error read_device_identification_map(nmbs_bitfield_256 map);

typedef struct {
    nmbs_platform_conf platform_conf;
    nmbs_callbacks callbacks;
    
    nmbs_t nmbs;

    int listen_sock;
    struct sockaddr_storage dest_addr;
} Modbus_t;

static Modbus_t mb;


int modbus_init() {
    for (int j = 5; j > 0; j--) {
	ESP_LOGE(TAG, "waiting... %d", j);
	vTaskDelay(pdMS_TO_TICKS(1000));
    }
    nmbs_platform_conf_create(&mb.platform_conf);
    mb.platform_conf.transport = NMBS_TRANSPORT_TCP;
    mb.platform_conf.read = modbus_read;
    mb.platform_conf.write = modbus_write;
    mb.platform_conf.arg = &mb;
    nmbs_callbacks_create(&mb.callbacks);

    mb.callbacks.read_coils = read_coils;
    mb.callbacks.read_discrete_inputs = read_discrete_inputs;
    mb.callbacks.read_holding_registers = read_holding_registers;
    mb.callbacks.read_input_registers = read_input_registers;
    mb.callbacks.write_single_coil = write_single_coil;
    mb.callbacks.write_single_register = write_single_register;
    mb.callbacks.write_multiple_coils = write_multiple_coils;
    mb.callbacks.write_multiple_registers = write_multiple_registers;
    mb.callbacks.read_file_record = read_file_record;
    mb.callbacks.write_file_record = write_file_record;
    mb.callbacks.read_device_identification = read_device_identification;
    mb.callbacks.read_device_identification_map = read_device_identification_map;

    
    nmbs_error err = nmbs_server_create(
	&mb.nmbs,
	0,
	&mb.platform_conf,
	&mb.callbacks);
    if (err != NMBS_ERROR_NONE) {
        ESP_LOGE(TAG, "Error creating modbus client error = %d\n", err);
        return 1;
    }
    // Set only the response timeout. Byte timeout will be handled by the TCP connection
    nmbs_set_read_timeout(&mb.nmbs, 1000);


    // open the socket
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&mb.dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(MODBUS_PORT);
    ip_protocol = IPPROTO_IP;
    mb.listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (mb.listen_sock < 0) {
	ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
	return 1;
    }
    
    int opt = 1;

    setsockopt(mb.listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGI(TAG, "Socket created");
    int ierr = bind(mb.listen_sock, (struct sockaddr *)&mb.dest_addr, sizeof(mb.dest_addr));
    if (ierr != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", MODBUS_PORT);
    
    ierr = listen(mb.listen_sock, 1);
    if (ierr != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    return 0;

    CLEAN_UP:
    close(mb.listen_sock);
    vTaskDelete(NULL);
    return 1;
}


void ModbusTask(void *parameters) {
    (void)parameters; // Avoid unused parameter warning
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    while (1) {
	int client_sock = accept(mb.listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
	if (client_sock < 0) {
	    perror("Socket accept failed");
	    close(mb.listen_sock);
	    vTaskDelete(NULL);
	}
	ESP_LOGE(TAG, "modbus task CONNECTED!");
	while(1) {
	    int bytes_sent = 0;
	    char buffer[1];
	    int bytes_received = recv(client_sock, buffer, sizeof(buffer), 0);
	    if (bytes_received == 0) {
		ESP_LOGE(TAG, "client closed connection");
		break;
	    }
	    if (bytes_received < 0) {
		ESP_LOGE(TAG, "error in recv %d", errno);
		break;
	    }
	    buffer[0] = toupper(buffer[0]);
	    bytes_sent = send(client_sock, buffer, bytes_received, 0);
	    if (bytes_sent < 0) {
		ESP_LOGE(TAG, "Send failed");
		break;
	    }
	    // vTaskDelay(pdMS_TO_TICKS(1000));
	}
    }
}


static int32_t modbus_read(uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    return 0;
}
static int32_t modbus_write(const uint8_t* buf, uint16_t count, int32_t byte_timeout_ms, void* arg) {
    return 0;
}
static nmbs_error read_coils(uint16_t address, uint16_t quantity, nmbs_bitfield coils_out, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_discrete_inputs(uint16_t address, uint16_t quantity, nmbs_bitfield inputs_out, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_holding_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_input_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error write_single_coil(uint16_t address, bool value, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error write_single_register(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error write_multiple_coils(uint16_t address, uint16_t quantity, const nmbs_bitfield coils, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error write_multiple_registers(uint16_t address, uint16_t quantity, const uint16_t* registers, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_file_record(uint16_t file_number, uint16_t record_number, uint16_t* registers, uint16_t count, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error write_file_record(uint16_t file_number, uint16_t record_number, const uint16_t* registers, uint16_t count, uint8_t unit_id, void* arg) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_device_identification(uint8_t object_id, char buffer[NMBS_DEVICE_IDENTIFICATION_STRING_LENGTH]) {
    return NMBS_ERROR_NONE;
}
static nmbs_error read_device_identification_map(nmbs_bitfield_256 map) {
    return NMBS_ERROR_NONE;
}
