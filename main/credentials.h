/*
 * credentials.h -- Runtime credential storage.
 *
 * WiFi and MQTT credentials are kept out of the source tree.  They are
 * written to the ESP32's NVS partition by the provision.sh script and
 * loaded at runtime into this struct.
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <stdbool.h>

typedef struct {
    char wifi_ssid[33];       /* up to 32 chars + null */
    char wifi_pass[65];       /* up to 64 chars + null */
    char mqtt_uri[128];       /* e.g. "mqtts://broker.hivemq.com:8883" */
    char mqtt_user[65];
    char mqtt_pass[65];
    char mqtt_client_id[33];
} device_credentials_t;

/*
 * Load all credential fields from the NVS "creds" namespace.
 * Returns false if any key is missing (run provision.sh first).
 */
bool credentials_load(device_credentials_t *creds);

#endif /* CREDENTIALS_H */
