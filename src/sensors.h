#ifndef __SENSORS_H__
#define __SENSORS_H__
#include <stdint.h>

void sensors_init(void);
float sensors_get(uint32_t sensor_id);
void SensorTask(void *parameters);

#endif
