#ifndef WP_MQTT_H
#define WP_MQTT_H

#include <stdbool.h>
#include <stddef.h>

bool wifi_connect(void);
void wifi_disconnect(void);
bool mqtt_connect(void);
bool mqtt_publish(const char *payload, size_t len);
void mqtt_disconnect(void);

#endif
