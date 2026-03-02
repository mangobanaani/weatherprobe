/*
 * battery.h -- Li-ion battery voltage monitor.
 *
 * Reads the battery voltage through an ADC channel connected to a
 * resistive voltage divider (100 k + 100 k).  Returns the actual
 * battery voltage and a linear state-of-charge percentage clamped
 * between BATTERY_EMPTY_V (0 %) and BATTERY_FULL_V (100 %).
 */

#ifndef BATTERY_H
#define BATTERY_H

typedef struct {
    float voltage;     /* actual battery voltage (V) after divider correction */
    int   percentage;  /* 0-100 linear estimate of state of charge            */
} battery_reading_t;

/* Configure ADC1 channel and optional line-fitting calibration */
void battery_init(void);

/* Sample 16 readings, average, calibrate, and return result */
battery_reading_t battery_read(void);

/* Release ADC and calibration resources */
void battery_deinit(void);

#endif /* BATTERY_H */
