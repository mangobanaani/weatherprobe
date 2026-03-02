#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <stdbool.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_uri[128];
    char mqtt_user[65];
    char mqtt_pass[65];
    char mqtt_client_id[33];
} device_credentials_t;

bool credentials_load(device_credentials_t *creds);

#endif
