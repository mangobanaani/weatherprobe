#!/bin/bash
set -euo pipefail

# Provisions WiFi and MQTT credentials into ESP32 NVS.
# Run once per device. Credentials are stored in flash,
# never in source code.
#
# Usage: ./provision.sh [port]
#   port: serial port (default: /dev/ttyUSB0)

PORT="${1:-/dev/ttyUSB0}"

read -rp "WiFi SSID: " WIFI_SSID
read -rsp "WiFi password: " WIFI_PASS; echo
read -rp "MQTT broker URI (e.g. mqtts://xyz.hivemq.cloud:8883): " MQTT_URI
read -rp "MQTT username: " MQTT_USER
read -rsp "MQTT password: " MQTT_PASS; echo
read -rp "MQTT client ID (e.g. weatherprobe-01): " MQTT_ID

TMPCSV=$(mktemp)
cat > "$TMPCSV" <<EOF
key,type,encoding,value
creds,namespace,,
wifi_ssid,data,string,"$WIFI_SSID"
wifi_pass,data,string,"$WIFI_PASS"
mqtt_uri,data,string,"$MQTT_URI"
mqtt_user,data,string,"$MQTT_USER"
mqtt_pass,data,string,"$MQTT_PASS"
mqtt_id,data,string,"$MQTT_ID"
EOF

echo "Writing credentials to NVS..."
python -m esptool --port "$PORT" erase_region 0x9000 0x6000
python -m nvs_partition_gen generate "$TMPCSV" nvs_creds.bin 0x6000
python -m esptool --port "$PORT" write_flash 0x9000 nvs_creds.bin

rm -f "$TMPCSV" nvs_creds.bin
echo "Done. Credentials flashed to NVS."
