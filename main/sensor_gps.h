#ifndef SENSOR_GPS_H
#define SENSOR_GPS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double latitude;
    double longitude;
    float  altitude_m;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t satellites;
    bool   fix_valid;
} gps_reading_t;

void gps_init(void);
gps_reading_t gps_read(void);

#endif
