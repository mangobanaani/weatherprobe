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
