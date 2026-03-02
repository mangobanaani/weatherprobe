#ifndef BATTERY_H
#define BATTERY_H

typedef struct {
    float voltage;
    int   percentage;
} battery_reading_t;

void battery_init(void);
battery_reading_t battery_read(void);

void battery_deinit(void);

#endif
