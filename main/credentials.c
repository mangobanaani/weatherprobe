#include "credentials.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CREDS";

static bool read_str(nvs_handle_t h, const char *key, char *out, size_t max)
{
    size_t len = max;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Missing NVS key '%s': %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool credentials_load(device_credentials_t *creds)
{
    memset(creds, 0, sizeof(*creds));

    nvs_handle_t h;
    esp_err_t err = nvs_open("creds", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS namespace 'creds' not found: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Run provision.sh to flash credentials");
        return false;
    }

    bool ok = true;
    ok = read_str(h, "wifi_ssid", creds->wifi_ssid, sizeof(creds->wifi_ssid)) && ok;
    ok = read_str(h, "wifi_pass", creds->wifi_pass, sizeof(creds->wifi_pass)) && ok;
    ok = read_str(h, "mqtt_uri", creds->mqtt_uri, sizeof(creds->mqtt_uri)) && ok;
    ok = read_str(h, "mqtt_user", creds->mqtt_user, sizeof(creds->mqtt_user)) && ok;
    ok = read_str(h, "mqtt_pass", creds->mqtt_pass, sizeof(creds->mqtt_pass)) && ok;
    ok = read_str(h, "mqtt_id", creds->mqtt_client_id, sizeof(creds->mqtt_client_id)) && ok;

    nvs_close(h);
    return ok;
}
