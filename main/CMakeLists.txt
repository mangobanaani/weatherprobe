idf_component_register(
    SRCS "main.c" "sensor_bme280.c" "battery.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_event mqtt spiffs esp_adc
)
