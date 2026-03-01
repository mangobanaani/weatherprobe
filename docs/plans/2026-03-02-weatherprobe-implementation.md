# WeatherProbe Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a solar-powered ESP32 weather station that reads BME280 + GPS data every 5 minutes and publishes to GCP Pub/Sub via MQTT.

**Architecture:** ESP-IDF C firmware using deep sleep wake cycles. BME280 on I2C, NEO-M8N on UART2, MQTT over TLS to HiveMQ Cloud. SPIFFS ring buffer for offline resilience. GCP Cloud Function bridges MQTT to Pub/Sub.

**Tech Stack:** ESP-IDF v5.x, C, MQTT (esp-mqtt), I2C (new driver API), UART, SPIFFS, GCP Cloud Functions (Python), GCP Pub/Sub

---

### Task 1: ESP-IDF Project Scaffold

**Files:**
- Create: `CMakeLists.txt`
- Create: `sdkconfig.defaults`
- Create: `partitions.csv`
- Create: `main/CMakeLists.txt`
- Create: `main/main.c`
- Create: `main/config.h`

**Step 1: Create top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weatherprobe)
```

**Step 2: Create sdkconfig.defaults**

```kconfig
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_SPIFFS_USE_MAGIC=y
CONFIG_SPIFFS_USE_MAGIC_LENGTH=y
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16
```

**Step 3: Create partitions.csv**

```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x1F0000,
storage,  data, spiffs,  0x200000,0x100000,
```

**Step 4: Create main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_event mqtt spiffs esp_adc
)
```

**Step 5: Create main/config.h**

```c
#ifndef CONFIG_H
#define CONFIG_H

// WiFi
#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASS           "YOUR_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS 15000

// MQTT
#define MQTT_BROKER_URI     "mqtts://broker.hivemq.com:8883"
#define MQTT_USERNAME       "your_user"
#define MQTT_PASSWORD       "your_pass"
#define MQTT_TOPIC          "weatherprobe/weatherprobe-01/data"
#define MQTT_CLIENT_ID      "weatherprobe-01"

// Device
#define DEVICE_ID           "weatherprobe-01"
#define SLEEP_DURATION_US   (5ULL * 60 * 1000000)  // 5 minutes

// BME280 (I2C)
#define BME280_I2C_PORT     I2C_NUM_0
#define BME280_I2C_SDA      GPIO_NUM_21
#define BME280_I2C_SCL      GPIO_NUM_22
#define BME280_I2C_ADDR     0x76
#define BME280_I2C_SPEED_HZ 400000

// GPS (UART2)
#define GPS_UART_PORT       UART_NUM_2
#define GPS_UART_TX         GPIO_NUM_17
#define GPS_UART_RX         GPIO_NUM_16
#define GPS_BAUD_RATE       9600
#define GPS_FIX_TIMEOUT_MS  30000
#define GPS_BUF_SIZE        512

// Battery ADC
#define BATTERY_ADC_GPIO    GPIO_NUM_34
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6
// Voltage divider: R1=100k, R2=100k -> factor = 2.0
#define BATTERY_DIVIDER     2.0f
#define BATTERY_FULL_V      4.2f
#define BATTERY_EMPTY_V     3.0f

// SPIFFS buffer
#define SPIFFS_BASE_PATH    "/spiffs"
#define SPIFFS_PARTITION    "storage"
#define BUFFER_FILE         "/spiffs/buffer.dat"
#define BUFFER_MAX_ENTRIES  100
#define BATCH_SEND_MAX      10

#endif
```

**Step 6: Create minimal main/main.c**

```c
#include <stdio.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "config.h"

static const char *TAG = "MAIN";

RTC_DATA_ATTR static uint32_t boot_count = 0;

void app_main(void)
{
    boot_count++;
    ESP_LOGI(TAG, "WeatherProbe boot #%lu", (unsigned long)boot_count);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Woke from timer");
    } else {
        ESP_LOGI(TAG, "Cold boot");
    }

    // TODO: read sensors, publish, sleep
    ESP_LOGI(TAG, "Entering deep sleep for %llu us", SLEEP_DURATION_US);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}
```

**Step 7: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 8: Commit**

```bash
git add CMakeLists.txt sdkconfig.defaults partitions.csv main/
git commit -m "scaffold esp-idf project with deep sleep cycle"
```

---

### Task 2: BME280 Sensor Driver

**Files:**
- Create: `main/sensor_bme280.h`
- Create: `main/sensor_bme280.c`
- Modify: `main/CMakeLists.txt` (add source file)

**Step 1: Create main/sensor_bme280.h**

```c
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
```

**Step 2: Create main/sensor_bme280.c**

```c
#include "sensor_bme280.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BME280";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

typedef struct {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint16_t dig_P1; int16_t dig_P2; int16_t dig_P3;
    int16_t  dig_P4; int16_t dig_P5; int16_t dig_P6;
    int16_t  dig_P7; int16_t dig_P8; int16_t dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3;
    int16_t  dig_H4; int16_t dig_H5; int8_t  dig_H6;
} bme280_calib_t;

static bme280_calib_t cal;
static int32_t t_fine;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
}

static void read_calibration(void)
{
    uint8_t buf[26];
    reg_read(0x88, buf, 26);

    cal.dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    cal.dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    cal.dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);
    cal.dig_P1 = (uint16_t)(buf[7] << 8 | buf[6]);
    cal.dig_P2 = (int16_t)(buf[9] << 8 | buf[8]);
    cal.dig_P3 = (int16_t)(buf[11] << 8 | buf[10]);
    cal.dig_P4 = (int16_t)(buf[13] << 8 | buf[12]);
    cal.dig_P5 = (int16_t)(buf[15] << 8 | buf[14]);
    cal.dig_P6 = (int16_t)(buf[17] << 8 | buf[16]);
    cal.dig_P7 = (int16_t)(buf[19] << 8 | buf[18]);
    cal.dig_P8 = (int16_t)(buf[21] << 8 | buf[20]);
    cal.dig_P9 = (int16_t)(buf[23] << 8 | buf[22]);

    uint8_t h1;
    reg_read(0xA1, &h1, 1);
    cal.dig_H1 = h1;

    uint8_t hbuf[7];
    reg_read(0xE1, hbuf, 7);
    cal.dig_H2 = (int16_t)(hbuf[1] << 8 | hbuf[0]);
    cal.dig_H3 = hbuf[2];
    cal.dig_H4 = (int16_t)((int16_t)hbuf[3] << 4 | (hbuf[4] & 0x0F));
    cal.dig_H5 = (int16_t)((int16_t)hbuf[5] << 4 | (hbuf[4] >> 4));
    cal.dig_H6 = (int8_t)hbuf[6];
}

static int32_t compensate_T(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)cal.dig_T1 << 1))) *
                    (int32_t)cal.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)cal.dig_T1) *
                      ((adc_T >> 4) - (int32_t)cal.dig_T1)) >> 12) *
                    (int32_t)cal.dig_T3) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

static uint32_t compensate_P(int32_t adc_P)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)cal.dig_P6;
    var2 = var2 + ((var1 * (int64_t)cal.dig_P5) << 17);
    var2 = var2 + (((int64_t)cal.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)cal.dig_P3) >> 8) +
           ((var1 * (int64_t)cal.dig_P2) << 12);
    var1 = ((((int64_t)1) << 47) + var1) * (int64_t)cal.dig_P1 >> 33;
    if (var1 == 0) return 0;
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)cal.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)cal.dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)cal.dig_P7) << 4);
    return (uint32_t)p;
}

static uint32_t compensate_H(int32_t adc_H)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_H << 14) - ((int32_t)cal.dig_H4 << 20) -
            (cal.dig_H5 * v)) + 16384) >> 15) *
         (((((((v * cal.dig_H6) >> 10) *
              (((v * cal.dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
            cal.dig_H2 + 8192) >> 14));
    v -= (((((v >> 15) * (v >> 15)) >> 7) * cal.dig_H1) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

void bme280_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BME280_I2C_PORT,
        .sda_io_num = BME280_I2C_SDA,
        .scl_io_num = BME280_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_I2C_ADDR,
        .scl_speed_hz = BME280_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    // Verify chip ID
    uint8_t chip_id;
    reg_read(0xD0, &chip_id, 1);
    if (chip_id != 0x60) {
        ESP_LOGE(TAG, "Unexpected chip ID: 0x%02x (expected 0x60)", chip_id);
    }

    // Soft reset
    reg_write(0xE0, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));

    read_calibration();
    ESP_LOGI(TAG, "Initialized");
}

bme280_reading_t bme280_read(void)
{
    bme280_reading_t r = {0};

    // Trigger forced measurement: osrs_h=x1, osrs_t=x1, osrs_p=x1, mode=forced
    reg_write(0xF2, 0x01);
    reg_write(0xF4, (0x01 << 5) | (0x01 << 2) | 0x01);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t raw[8];
    esp_err_t err = reg_read(0xF7, raw, 8);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        return r;
    }

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];

    int32_t t_raw = compensate_T(adc_T);
    uint32_t p_raw = compensate_P(adc_P);
    uint32_t h_raw = compensate_H(adc_H);

    r.temperature_c = t_raw / 100.0f;
    r.pressure_hpa = (p_raw / 256.0f) / 100.0f;
    r.humidity_pct = h_raw / 1024.0f;
    r.valid = true;

    ESP_LOGI(TAG, "T=%.2fC H=%.1f%% P=%.1fhPa",
             r.temperature_c, r.humidity_pct, r.pressure_hpa);
    return r;
}
```

**Step 3: Update main/CMakeLists.txt to include new source**

```cmake
idf_component_register(
    SRCS "main.c" "sensor_bme280.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_event mqtt spiffs esp_adc
)
```

**Step 4: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 5: Commit**

```bash
git add main/sensor_bme280.c main/sensor_bme280.h main/CMakeLists.txt
git commit -m "add BME280 I2C driver with compensation"
```

---

### Task 3: GPS NMEA Parser

**Files:**
- Create: `main/sensor_gps.h`
- Create: `main/sensor_gps.c`
- Modify: `main/CMakeLists.txt` (add source file)

**Step 1: Create main/sensor_gps.h**

```c
#ifndef SENSOR_GPS_H
#define SENSOR_GPS_H

#include <stdbool.h>

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
```

**Step 2: Create main/sensor_gps.c**

```c
#include "sensor_gps.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "GPS";

static double parse_ddmm(const char *s)
{
    if (!s || !*s) return 0.0;
    double raw = atof(s);
    int deg = (int)(raw / 100);
    return deg + (raw - deg * 100) / 60.0;
}

static bool nmea_checksum_ok(const char *sentence)
{
    if (sentence[0] != '$') return false;
    const char *p = sentence + 1;
    uint8_t crc = 0;
    while (*p && *p != '*') crc ^= (uint8_t)*p++;
    if (*p != '*') return false;
    char hex[3] = {p[1], p[2], 0};
    return crc == (uint8_t)strtol(hex, NULL, 16);
}

static bool parse_gga(char *s, gps_reading_t *out)
{
    char *fields[15];
    int n = 0;
    for (char *tok = strtok(s, ","); tok && n < 15; tok = strtok(NULL, ","))
        fields[n++] = tok;
    if (n < 10) return false;

    double t = atof(fields[1]);
    out->hour = (uint8_t)(t / 10000);
    out->min = (uint8_t)((int)t % 10000 / 100);
    out->sec = (uint8_t)((int)t % 100);

    int fix_quality = atoi(fields[6]);
    out->fix_valid = fix_quality > 0;
    out->satellites = (uint8_t)atoi(fields[7]);
    out->latitude = parse_ddmm(fields[2]);
    if (n > 3 && fields[3][0] == 'S') out->latitude = -out->latitude;
    out->longitude = parse_ddmm(fields[4]);
    if (n > 5 && fields[5][0] == 'W') out->longitude = -out->longitude;
    out->altitude_m = (float)atof(fields[9]);
    return true;
}

void gps_init(void)
{
    uart_config_t cfg = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_PORT, GPS_UART_TX, GPS_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_PORT, GPS_BUF_SIZE * 2,
                                        0, 0, NULL, 0));
    ESP_LOGI(TAG, "Initialized on UART%d", GPS_UART_PORT);
}

gps_reading_t gps_read(void)
{
    gps_reading_t result = {0};
    uint8_t buf[GPS_BUF_SIZE];
    char line[GPS_BUF_SIZE];
    int line_pos = 0;

    int64_t start = esp_timer_get_time();
    int64_t timeout = (int64_t)GPS_FIX_TIMEOUT_MS * 1000;

    while ((esp_timer_get_time() - start) < timeout) {
        int len = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)buf[i];
            if (c == '$') line_pos = 0;
            if (line_pos < GPS_BUF_SIZE - 1) {
                line[line_pos++] = c;
            }
            if (c == '\n' && line_pos > 6) {
                line[line_pos] = '\0';

                if (!nmea_checksum_ok(line)) {
                    line_pos = 0;
                    continue;
                }

                if (strncmp(line, "$GPGGA", 6) == 0 ||
                    strncmp(line, "$GNGGA", 6) == 0) {
                    char tmp[GPS_BUF_SIZE];
                    strncpy(tmp, line, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    char *star = strchr(tmp, '*');
                    if (star) *star = '\0';

                    if (parse_gga(tmp, &result) && result.fix_valid) {
                        ESP_LOGI(TAG, "Fix: lat=%.6f lon=%.6f alt=%.1fm sats=%d",
                                 result.latitude, result.longitude,
                                 result.altitude_m, result.satellites);
                        uart_driver_delete(GPS_UART_PORT);
                        return result;
                    }
                }
                line_pos = 0;
            }
        }
    }

    ESP_LOGW(TAG, "No fix within %d ms", GPS_FIX_TIMEOUT_MS);
    uart_driver_delete(GPS_UART_PORT);
    return result;
}
```

**Step 3: Update main/CMakeLists.txt**

Add `"sensor_gps.c"` to the SRCS list.

**Step 4: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 5: Commit**

```bash
git add main/sensor_gps.c main/sensor_gps.h main/CMakeLists.txt
git commit -m "add GPS NMEA parser for NEO-M8N over UART2"
```

---

### Task 4: Battery Monitor

**Files:**
- Create: `main/battery.h`
- Create: `main/battery.c`
- Modify: `main/CMakeLists.txt` (add source file)

**Step 1: Create main/battery.h**

```c
#ifndef BATTERY_H
#define BATTERY_H

typedef struct {
    float voltage;
    int   percentage;
} battery_reading_t;

void battery_init(void);
battery_reading_t battery_read(void);

#endif
```

**Step 2: Create main/battery.c**

```c
#include "battery.h"
#include "config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BATT";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg));

    // Try line fitting calibration
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values");
        s_cali = NULL;
    }

    ESP_LOGI(TAG, "Initialized on ADC1 channel %d", BATTERY_ADC_CHANNEL);
}

battery_reading_t battery_read(void)
{
    battery_reading_t r = {0};

    // Average 16 samples
    int32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        int raw;
        adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw);
        sum += raw;
    }

    int mv;
    if (s_cali) {
        int avg_raw = sum / 16;
        adc_cali_raw_to_voltage(s_cali, avg_raw, &mv);
    } else {
        // Rough estimate: 12-bit, 3.3V ref with 12dB attenuation
        mv = (int)((sum / 16) * 3300 / 4095);
    }

    r.voltage = (mv / 1000.0f) * BATTERY_DIVIDER;

    // Linear percentage between empty and full
    float pct = (r.voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    r.percentage = (int)pct;

    ESP_LOGI(TAG, "%.2fV (%d%%)", r.voltage, r.percentage);
    return r;
}
```

**Step 3: Update main/CMakeLists.txt**

Add `"battery.c"` to the SRCS list.

**Step 4: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 5: Commit**

```bash
git add main/battery.c main/battery.h main/CMakeLists.txt
git commit -m "add battery voltage monitor via ADC"
```

---

### Task 5: SPIFFS Data Buffer

**Files:**
- Create: `main/data_buffer.h`
- Create: `main/data_buffer.c`
- Modify: `main/CMakeLists.txt` (add source file)

**Step 1: Create main/data_buffer.h**

```c
#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

void buffer_init(void);
bool buffer_write(const char *json, size_t len);
int  buffer_count(void);
bool buffer_peek(char *out, size_t out_size);
void buffer_pop(void);

#endif
```

**Step 2: Create main/data_buffer.c**

This implements a simple file-based FIFO. Each entry is stored as a length-prefixed record in a flat file.

```c
#include "data_buffer.h"
#include "config.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "BUFFER";

void buffer_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_PARTITION,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    size_t total, used;
    esp_spiffs_info(SPIFFS_PARTITION, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", used, total);
}

bool buffer_write(const char *json, size_t len)
{
    if (buffer_count() >= BUFFER_MAX_ENTRIES) {
        ESP_LOGW(TAG, "Buffer full, dropping oldest");
        buffer_pop();
    }

    FILE *f = fopen(BUFFER_FILE, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open buffer for append");
        return false;
    }

    uint16_t slen = (uint16_t)len;
    fwrite(&slen, sizeof(slen), 1, f);
    fwrite(json, 1, len, f);
    fclose(f);

    ESP_LOGI(TAG, "Buffered %d bytes (%d entries)", (int)len, buffer_count());
    return true;
}

int buffer_count(void)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return 0;

    int count = 0;
    uint16_t slen;
    while (fread(&slen, sizeof(slen), 1, f) == 1) {
        fseek(f, slen, SEEK_CUR);
        count++;
    }
    fclose(f);
    return count;
}

bool buffer_peek(char *out, size_t out_size)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return false;

    uint16_t slen;
    if (fread(&slen, sizeof(slen), 1, f) != 1) {
        fclose(f);
        return false;
    }
    if (slen >= out_size) {
        fclose(f);
        return false;
    }
    fread(out, 1, slen, f);
    out[slen] = '\0';
    fclose(f);
    return true;
}

void buffer_pop(void)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return;

    // Read first entry to skip it
    uint16_t slen;
    if (fread(&slen, sizeof(slen), 1, f) != 1) {
        fclose(f);
        remove(BUFFER_FILE);
        return;
    }
    fseek(f, slen, SEEK_CUR);

    // Read remaining data
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    long remaining = end - pos;

    if (remaining <= 0) {
        fclose(f);
        remove(BUFFER_FILE);
        return;
    }

    char *tmp = malloc(remaining);
    if (!tmp) {
        fclose(f);
        return;
    }
    fseek(f, pos, SEEK_SET);
    fread(tmp, 1, remaining, f);
    fclose(f);

    // Rewrite file without first entry
    f = fopen(BUFFER_FILE, "wb");
    if (f) {
        fwrite(tmp, 1, remaining, f);
        fclose(f);
    }
    free(tmp);
}
```

**Step 3: Update main/CMakeLists.txt**

Add `"data_buffer.c"` to the SRCS list.

**Step 4: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 5: Commit**

```bash
git add main/data_buffer.c main/data_buffer.h main/CMakeLists.txt
git commit -m "add SPIFFS ring buffer for offline data storage"
```

---

### Task 6: WiFi + MQTT Client

**Files:**
- Create: `main/mqtt_client.h` (note: the header name differs from ESP-IDF's `mqtt_client.h` — use `wp_mqtt.h` to avoid collision)
- Create: `main/wp_mqtt.c`
- Create: `main/wp_mqtt.h`
- Modify: `main/CMakeLists.txt` (add source file)

**Step 1: Create main/wp_mqtt.h**

```c
#ifndef WP_MQTT_H
#define WP_MQTT_H

#include <stdbool.h>
#include <stddef.h>

bool wifi_connect(void);
void wifi_disconnect(void);
bool mqtt_connect(void);
bool mqtt_publish(const char *payload, size_t len);
void mqtt_disconnect(void);

#endif
```

**Step 2: Create main/wp_mqtt.c**

```c
#include "wp_mqtt.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "MQTT";

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT0
#define MQTT_PUBLISHED_BIT BIT1
#define MQTT_FAILED_BIT    BIT2

static EventGroupHandle_t s_wifi_eg;
static EventGroupHandle_t s_mqtt_eg;
static esp_mqtt_client_handle_t s_client;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_mqtt_eg, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Published msg_id=%d", event->msg_id);
            xEventGroupSetBits(s_mqtt_eg, MQTT_PUBLISHED_BIT);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            xEventGroupSetBits(s_mqtt_eg, MQTT_FAILED_BIT);
            break;
        default:
            break;
    }
}

bool wifi_connect(void)
{
    // Init NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    s_wifi_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL));

    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connect timeout");
        return false;
    }
    return true;
}

void wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    vEventGroupDelete(s_wifi_eg);
}

bool mqtt_connect(void)
{
    s_mqtt_eg = xEventGroupCreate();

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials = {
            .username = MQTT_USERNAME,
            .authentication.password = MQTT_PASSWORD,
            .client_id = MQTT_CLIENT_ID,
        },
        .session.keepalive = 30,
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_eg, MQTT_CONNECTED_BIT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(10000));
    if (!(bits & MQTT_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "MQTT connect timeout");
        return false;
    }
    return true;
}

bool mqtt_publish(const char *payload, size_t len)
{
    xEventGroupClearBits(s_mqtt_eg, MQTT_PUBLISHED_BIT | MQTT_FAILED_BIT);

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC,
                                          payload, (int)len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed");
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_eg,
                                            MQTT_PUBLISHED_BIT | MQTT_FAILED_BIT,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(5000));
    return (bits & MQTT_PUBLISHED_BIT) != 0;
}

void mqtt_disconnect(void)
{
    esp_mqtt_client_disconnect(s_client);
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    vEventGroupDelete(s_mqtt_eg);
}
```

**Step 3: Update main/CMakeLists.txt**

Add `"wp_mqtt.c"` to the SRCS list.

**Step 4: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 5: Commit**

```bash
git add main/wp_mqtt.c main/wp_mqtt.h main/CMakeLists.txt
git commit -m "add WiFi and MQTT client with TLS support"
```

---

### Task 7: Integrate Main Wake Cycle

**Files:**
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt` (final SRCS list)

**Step 1: Update main/CMakeLists.txt with all sources**

```cmake
idf_component_register(
    SRCS "main.c" "sensor_bme280.c" "sensor_gps.c" "battery.c"
         "data_buffer.c" "wp_mqtt.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_event mqtt spiffs esp_adc esp_timer
)
```

**Step 2: Rewrite main/main.c with full wake cycle**

```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "config.h"
#include "sensor_bme280.h"
#include "sensor_gps.h"
#include "battery.h"
#include "data_buffer.h"
#include "wp_mqtt.h"

static const char *TAG = "MAIN";

RTC_DATA_ATTR static uint32_t boot_count = 0;
RTC_DATA_ATTR static double last_lat = 0.0;
RTC_DATA_ATTR static double last_lon = 0.0;
RTC_DATA_ATTR static float last_alt = 0.0f;

static int format_payload(char *buf, size_t size,
                           const bme280_reading_t *bme,
                           const gps_reading_t *gps,
                           const battery_reading_t *batt)
{
    return snprintf(buf, size,
        "{\"device_id\":\"%s\","
        "\"ts\":%lld,"
        "\"temp_c\":%.2f,"
        "\"humidity_pct\":%.1f,"
        "\"pressure_hpa\":%.2f,"
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"alt_m\":%.1f,"
        "\"battery_pct\":%d,"
        "\"battery_v\":%.2f,"
        "\"gps_fix\":%s}",
        DEVICE_ID,
        (long long)(esp_timer_get_time() / 1000000),
        bme->temperature_c,
        bme->humidity_pct,
        bme->pressure_hpa,
        gps->fix_valid ? gps->latitude : last_lat,
        gps->fix_valid ? gps->longitude : last_lon,
        gps->fix_valid ? gps->altitude_m : last_alt,
        batt->percentage,
        batt->voltage,
        gps->fix_valid ? "true" : "false");
}

void app_main(void)
{
    boot_count++;
    ESP_LOGI(TAG, "Boot #%lu", (unsigned long)boot_count);

    // 1. Init and read sensors
    bme280_init();
    bme280_reading_t bme = bme280_read();

    gps_init();
    gps_reading_t gps = gps_read();

    // Update last known position
    if (gps.fix_valid) {
        last_lat = gps.latitude;
        last_lon = gps.longitude;
        last_alt = gps.altitude_m;
    }

    battery_init();
    battery_reading_t batt = battery_read();

    // 2. Format payload
    char payload[512];
    int len = format_payload(payload, sizeof(payload), &bme, &gps, &batt);

    // 3. Init SPIFFS buffer
    buffer_init();

    // 4. Connect and publish
    bool sent = false;
    if (wifi_connect()) {
        if (mqtt_connect()) {
            // Send current reading
            sent = mqtt_publish(payload, len);

            // Drain buffer if we have connectivity
            if (sent) {
                char buffered[512];
                int drained = 0;
                while (drained < BATCH_SEND_MAX && buffer_peek(buffered, sizeof(buffered))) {
                    if (mqtt_publish(buffered, strlen(buffered))) {
                        buffer_pop();
                        drained++;
                    } else {
                        break;
                    }
                }
                if (drained > 0) {
                    ESP_LOGI(TAG, "Drained %d buffered readings", drained);
                }
            }
            mqtt_disconnect();
        }
        wifi_disconnect();
    }

    // 5. Buffer if send failed
    if (!sent) {
        ESP_LOGW(TAG, "Publish failed, buffering locally");
        buffer_write(payload, len);
    }

    // 6. Sleep
    ESP_LOGI(TAG, "Sleeping for %llu us", SLEEP_DURATION_US);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}
```

**Step 3: Verify build compiles**

Run: `idf.py build`
Expected: BUILD SUCCESS

**Step 4: Commit**

```bash
git add main/main.c main/CMakeLists.txt
git commit -m "integrate full wake cycle: read, publish, buffer, sleep"
```

---

### Task 8: GCP Cloud Function Bridge

**Files:**
- Create: `cloud/cloud_function/main.py`
- Create: `cloud/cloud_function/requirements.txt`

**Step 1: Create cloud/cloud_function/requirements.txt**

```
paho-mqtt==2.1.0
google-cloud-pubsub==2.23.0
functions-framework==3.5.0
```

**Step 2: Create cloud/cloud_function/main.py**

```python
import json
import os
import threading

import functions_framework
import paho.mqtt.client as mqtt
from google.cloud import pubsub_v1

MQTT_BROKER = os.environ.get("MQTT_BROKER", "broker.hivemq.com")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "weatherprobe/#")

GCP_PROJECT = os.environ.get("GCP_PROJECT", "")
PUBSUB_TOPIC = os.environ.get("PUBSUB_TOPIC", "weatherprobe-readings")

publisher = pubsub_v1.PublisherClient()
topic_path = publisher.topic_path(GCP_PROJECT, PUBSUB_TOPIC)


def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        # Validate JSON
        json.loads(payload)
        future = publisher.publish(topic_path, payload.encode("utf-8"))
        future.result(timeout=10)
        print(f"Published to Pub/Sub: {msg.topic}")
    except Exception as e:
        print(f"Error forwarding message: {e}")


def start_mqtt():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set()
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.subscribe(MQTT_TOPIC, qos=1)
    client.loop_forever()


# For Cloud Run (long-running)
@functions_framework.http
def health(request):
    return "OK", 200


# Start MQTT listener in background thread
mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
mqtt_thread.start()
```

**Step 3: Commit**

```bash
git add cloud/
git commit -m "add GCP Cloud Function MQTT-to-Pub/Sub bridge"
```

---

### Task 9: Pub/Sub Setup Script

**Files:**
- Create: `cloud/pubsub_setup.sh`

**Step 1: Create cloud/pubsub_setup.sh**

```bash
#!/bin/bash
set -euo pipefail

PROJECT_ID="${1:?Usage: $0 <gcp-project-id>}"

echo "Creating Pub/Sub topic..."
gcloud pubsub topics create weatherprobe-readings \
    --project="$PROJECT_ID" \
    2>/dev/null || echo "Topic already exists"

echo "Creating dead-letter topic..."
gcloud pubsub topics create weatherprobe-dead-letter \
    --project="$PROJECT_ID" \
    2>/dev/null || echo "Dead letter topic already exists"

echo "Creating subscription for downstream processing..."
gcloud pubsub subscriptions create weatherprobe-sub \
    --topic=weatherprobe-readings \
    --project="$PROJECT_ID" \
    --ack-deadline=60 \
    --dead-letter-topic=weatherprobe-dead-letter \
    --max-delivery-attempts=5 \
    2>/dev/null || echo "Subscription already exists"

echo "Done. Topic: projects/$PROJECT_ID/topics/weatherprobe-readings"
```

**Step 2: Commit**

```bash
git add cloud/pubsub_setup.sh
git commit -m "add Pub/Sub topic setup script"
```

---

### Task 10: Final Verification and README

**Step 1: Verify full build**

Run: `idf.py build`
Expected: BUILD SUCCESS with no warnings

**Step 2: Flash to device (when hardware is ready)**

Run: `idf.py flash monitor`
Expected: See boot log, sensor readings, MQTT connection

**Step 3: Commit final state**

```bash
git add -A
git commit -m "complete weatherprobe firmware and cloud bridge"
```
