/*
 * sensor_bme280.h -- BME280 temperature / humidity / pressure driver.
 *
 * Communicates with a Bosch BME280 sensor over I2C.  Calibration
 * coefficients are read once during init and applied to every
 * subsequent forced-mode measurement using the Bosch integer
 * compensation formulae.
 */

#ifndef SENSOR_BME280_H
#define SENSOR_BME280_H

#include <stdbool.h>

typedef struct {
    float temperature_c;   /* degrees Celsius        */
    float humidity_pct;    /* relative humidity (%)   */
    float pressure_hpa;    /* atmospheric pressure (hPa) */
    bool  valid;           /* false if read failed    */
} bme280_reading_t;

/* Initialise I2C bus, verify chip ID 0x60, read calibration data */
void bme280_init(void);

/* Trigger a forced measurement and return compensated values */
bme280_reading_t bme280_read(void);

/* Release I2C bus resources */
void bme280_deinit(void);

#endif /* SENSOR_BME280_H */
