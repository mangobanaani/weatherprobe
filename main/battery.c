/*
 * battery.c -- ADC-based battery voltage monitor.
 *
 * Uses the ESP-IDF oneshot ADC API on ADC1 channel 6 (GPIO34) with
 * 12 dB attenuation (0-2.5 V measurable range).  Sixteen consecutive
 * samples are averaged to reduce noise, then optionally calibrated
 * using eFuse line-fitting data when available.
 *
 * The raw millivolt value is multiplied by BATTERY_DIVIDER to recover
 * the true battery voltage, and a simple linear mapping between
 * BATTERY_EMPTY_V and BATTERY_FULL_V gives the percentage.
 */

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

    /* Use eFuse line-fitting calibration if the chip has it */
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

    /* Average 16 consecutive samples for noise reduction */
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
        /* Rough estimate: 12-bit range, 3.3 V reference at 12 dB atten */
        mv = (int)((sum / 16) * 3300 / 4095);
    }

    /* Scale by the voltage divider ratio to get the true battery voltage */
    r.voltage = (mv / 1000.0f) * BATTERY_DIVIDER;

    /* Linear percentage between empty and full thresholds */
    float pct = (r.voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    r.percentage = (int)pct;

    ESP_LOGI(TAG, "%.2fV (%d%%)", r.voltage, r.percentage);
    return r;
}

void battery_deinit(void)
{
    adc_oneshot_del_unit(s_adc);
    if (s_cali) {
        adc_cali_delete_scheme_line_fitting(s_cali);
        s_cali = NULL;
    }
}
