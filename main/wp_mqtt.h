/*
 * wp_mqtt.h -- WiFi station and MQTT client for the WeatherProbe.
 *
 * Provides a sequential connect-publish-disconnect workflow:
 *   1. wifi_connect()   -- associate with the AP, wait for an IP
 *   2. mqtt_connect()   -- open a TLS connection to the MQTT broker
 *   3. mqtt_publish()   -- publish one or more QoS 1 messages
 *   4. mqtt_disconnect() / wifi_disconnect()
 *
 * TLS is handled by the ESP-IDF certificate bundle (Mozilla CA roots).
 */

#ifndef WP_MQTT_H
#define WP_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include "credentials.h"

/* Connect to the WiFi AP specified in creds (blocks up to WIFI_CONNECT_TIMEOUT_MS) */
bool wifi_connect(const device_credentials_t *creds);

/* Tear down WiFi, event handlers, and the default netif */
void wifi_disconnect(void);

/* Open a TLS MQTT session to the broker URI in creds (10 s timeout) */
bool mqtt_connect(const device_credentials_t *creds, const char *topic);

/* Publish a single message on the topic set during mqtt_connect (QoS 1, 5 s ack) */
bool mqtt_publish(const char *payload, size_t len);

/* Gracefully disconnect and free the MQTT client */
void mqtt_disconnect(void);

#endif /* WP_MQTT_H */
