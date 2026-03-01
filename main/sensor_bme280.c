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
