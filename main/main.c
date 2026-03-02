/*
 * main.c -- WeatherProbe firmware entry point.
 *
 * Implements the deep-sleep wake cycle that runs every 5 minutes:
 *   1. Read BME280 (temperature, humidity, pressure)
 *   2. Read GPS   (latitude, longitude, altitude)
 *   3. Read battery voltage via ADC
 *   4. Connect WiFi, synchronise time via SNTP
 *   5. Publish a JSON reading to the MQTT broker
 *   6. Drain any previously buffered readings
 *   7. If publish fails, store the reading in the SPIFFS ring buffer
 *   8. Enter deep sleep
 *
 * RTC memory is used to preserve the boot counter and last-known GPS
 * position across sleep cycles so that a reading can still include
 * location data when the GPS module cannot obtain a fix.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "config.h"
#include "credentials.h"
#include "sensor_bme280.h"
#include "sensor_gps.h"
#include "battery.h"
#include "data_buffer.h"
#include "wp_mqtt.h"

static const char *TAG = "MAIN";

/* RTC-retained state -- survives deep sleep */
RTC_DATA_ATTR static uint32_t boot_count = 0;
RTC_DATA_ATTR static double last_lat = 0.0;
RTC_DATA_ATTR static double last_lon = 0.0;
RTC_DATA_ATTR static float last_alt = 0.0f;

/*
 * sync_time -- Polls pool.ntp.org for up to 10 s to set the system clock.
 * Called once per wake cycle immediately after WiFi association.
 */
static void sync_time(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 10) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    esp_sntp_stop();

    if (retry >= 10) {
        ESP_LOGW(TAG, "SNTP sync failed");
    } else {
        ESP_LOGI(TAG, "Time synchronized");
    }
}

/*
 * format_payload -- Serialise all sensor readings into a JSON string.
 *
 * If the current GPS fix is invalid, the last-known coordinates stored
 * in RTC memory are substituted so that every payload has a location.
 *
 * Returns the number of characters written (excluding the terminator),
 * or a negative value on encoding error.
 */
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
        (long long)time(NULL),
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

    /* 1. Initialise NVS and load WiFi/MQTT credentials */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    device_credentials_t creds;
    if (!credentials_load(&creds)) {
        ESP_LOGE(TAG, "No credentials in NVS, sleeping");
        esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
        esp_deep_sleep_start();
    }

    /* 2. Read all sensors */
    bme280_init();
    bme280_reading_t bme = bme280_read();

    gps_init();
    gps_reading_t gps = gps_read();

    if (gps.fix_valid) {
        last_lat = gps.latitude;
        last_lon = gps.longitude;
        last_alt = gps.altitude_m;
    }

    battery_init();
    battery_reading_t batt = battery_read();

    bme280_deinit();
    battery_deinit();

    /* 3. Mount SPIFFS offline buffer */
    buffer_init();

    /* 4. Bring up WiFi and synchronise the system clock */
    bool wifi_ok = wifi_connect(&creds);
    if (wifi_ok) {
        sync_time();
    }

    /* 5. Build JSON payload */
    char payload[512];
    int len = format_payload(payload, sizeof(payload), &bme, &gps, &batt);
    if (len < 0 || (size_t)len >= sizeof(payload)) {
        ESP_LOGE(TAG, "Payload format error or too large (%d), skipping", len);
        goto sleep;
    }

    /* 6. Publish to MQTT; on success, drain buffered readings */
    char topic[128];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_FMT, DEVICE_ID);

    bool sent = false;
    if (wifi_ok) {
        if (mqtt_connect(&creds, topic)) {
            sent = mqtt_publish(payload, len);

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

    /* 7. Store locally if the publish attempt failed */
    if (!sent) {
        ESP_LOGW(TAG, "Publish failed, buffering locally");
        buffer_write(payload, len);
    }

    buffer_deinit();

sleep:
    /* 8. Enter deep sleep until the next measurement interval */
    ESP_LOGI(TAG, "Sleeping for %llu us", SLEEP_DURATION_US);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}
