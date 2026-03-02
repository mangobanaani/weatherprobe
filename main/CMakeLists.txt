idf_component_register(
    SRCS "main.c" "sensor_bme280.c" "sensor_gps.c" "battery.c"
         "data_buffer.c" "wp_mqtt.c" "credentials.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_event mqtt spiffs esp_adc esp_timer esp_netif_sntp esp_tls
)
