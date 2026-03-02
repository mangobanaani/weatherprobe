/*
 * sensor_gps.h -- NEO-M8N GPS receiver driver (NMEA over UART).
 *
 * Reads UART data until a valid GGA sentence with a position fix is
 * received, or until GPS_FIX_TIMEOUT_MS elapses.  Supports both
 * $GPGGA (GPS-only) and $GNGGA (multi-constellation) sentences.
 */

#ifndef SENSOR_GPS_H
#define SENSOR_GPS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double  latitude;      /* decimal degrees, negative = south */
    double  longitude;     /* decimal degrees, negative = west  */
    float   altitude_m;    /* metres above mean sea level       */
    uint8_t hour;          /* UTC time from the GGA sentence    */
    uint8_t min;
    uint8_t sec;
    uint8_t satellites;    /* number of satellites in use        */
    bool    fix_valid;     /* true when fix quality > 0          */
} gps_reading_t;

/* Configure UART and install the driver */
void gps_init(void);

/* Block until a valid fix arrives or the timeout expires */
gps_reading_t gps_read(void);

#endif /* SENSOR_GPS_H */
