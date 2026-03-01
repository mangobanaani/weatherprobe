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

extern const uint8_t mqtt_ca_pem_start[] asm("_binary_isrg_root_x1_pem_start");
extern const uint8_t mqtt_ca_pem_end[]   asm("_binary_isrg_root_x1_pem_end");

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
        wifi_disconnect();
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
        .broker = {
            .address.uri = MQTT_BROKER_URI,
            .verification.certificate = (const char *)mqtt_ca_pem_start,
        },
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
        mqtt_disconnect();
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
