#ifndef WP_MQTT_H
#define WP_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include "credentials.h"

bool wifi_connect(const device_credentials_t *creds);
void wifi_disconnect(void);
bool mqtt_connect(const device_credentials_t *creds, const char *topic);
bool mqtt_publish(const char *payload, size_t len);
void mqtt_disconnect(void);

#endif
