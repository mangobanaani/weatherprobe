#ifndef SENSOR_BME280_H
#define SENSOR_BME280_H

#include <stdbool.h>

typedef struct {
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    bool  valid;
} bme280_reading_t;

void bme280_init(void);
bme280_reading_t bme280_read(void);

#endif
