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
