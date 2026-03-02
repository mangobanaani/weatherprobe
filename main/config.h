/*
 * config.h -- Central configuration for the WeatherProbe firmware.
 *
 * All hardware pin assignments, peripheral parameters, timing constants,
 * and SPIFFS buffer limits are collected here so that a single header
 * controls the entire build.  WiFi and MQTT secrets are NOT compiled in;
 * they are loaded from NVS at runtime (see credentials.h / provision.sh).
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ---- WiFi ---- */
#define WIFI_CONNECT_TIMEOUT_MS 15000

/* ---- MQTT ---- */
/* Topic template -- the %s is replaced with DEVICE_ID at runtime */
#define MQTT_TOPIC_FMT      "weatherprobe/%s/data"

/* ---- Device identity & sleep ---- */
#define DEVICE_ID           "weatherprobe-01"
#define SLEEP_DURATION_US   (5ULL * 60 * 1000000)  /* 5 minutes */

/* ---- BME280 environmental sensor (I2C bus 0) ---- */
#define BME280_I2C_PORT     I2C_NUM_0
#define BME280_I2C_SDA      GPIO_NUM_21
#define BME280_I2C_SCL      GPIO_NUM_22
#define BME280_I2C_ADDR     0x76
#define BME280_I2C_SPEED_HZ 400000

/* ---- NEO-M8N GPS receiver (UART2) ---- */
#define GPS_UART_PORT       UART_NUM_2
#define GPS_UART_TX         GPIO_NUM_17
#define GPS_UART_RX         GPIO_NUM_16
#define GPS_BAUD_RATE       9600
#define GPS_FIX_TIMEOUT_MS  30000
#define GPS_BUF_SIZE        512

/* ---- Battery ADC (GPIO34 / ADC1 channel 6) ---- */
#define BATTERY_ADC_GPIO    GPIO_NUM_34
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6
/* Voltage divider: R1 = 100 k, R2 = 100 k  =>  factor = 2.0 */
#define BATTERY_DIVIDER     2.0f
#define BATTERY_FULL_V      4.2f
#define BATTERY_EMPTY_V     3.0f

/* ---- SPIFFS offline data buffer ---- */
#define SPIFFS_BASE_PATH    "/spiffs"
#define SPIFFS_PARTITION    "storage"
#define BUFFER_FILE         "/spiffs/buffer.dat"
#define BUFFER_MAX_ENTRIES  100
#define BATCH_SEND_MAX      10

#endif /* CONFIG_H */
