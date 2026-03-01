#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_sntp.h"
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

    // 2. Init SPIFFS buffer
    buffer_init();

    // 3. Connect WiFi and sync time
    bool wifi_ok = wifi_connect();
    if (wifi_ok) {
        sync_time();
    }

    // 4. Format payload (now has correct timestamp if SNTP succeeded)
    char payload[512];
    int len = format_payload(payload, sizeof(payload), &bme, &gps, &batt);
    if (len < 0 || (size_t)len >= sizeof(payload)) {
        ESP_LOGE(TAG, "Payload format error or too large (%d)", len);
        len = (int)strlen(payload);
    }

    // 5. Connect MQTT, publish, drain
    bool sent = false;
    if (wifi_ok) {
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

    // 6. Buffer if send failed
    if (!sent) {
        ESP_LOGW(TAG, "Publish failed, buffering locally");
        buffer_write(payload, len);
    }

    // 7. Sleep
    ESP_LOGI(TAG, "Sleeping for %llu us", SLEEP_DURATION_US);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}
