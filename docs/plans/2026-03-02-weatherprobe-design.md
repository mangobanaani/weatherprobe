# WeatherProbe Design

Solar-powered ESP32 weather station that collects temperature, humidity, pressure, and GPS location data every 5 minutes and sends it to GCP Pub/Sub via MQTT.

## Hardware

- **MCU:** ESP32-WROOM-32 DevKit
- **Sensor:** BME280 (temperature, humidity, barometric pressure) over I2C
- **GPS:** NEO-M8N over UART2
- **Power:** 18650 Li-ion battery + 6V solar panel + TP4056 charge controller
- **Battery monitoring:** Voltage divider to ADC GPIO34

### Pin Assignments

| Component    | ESP32 Pin       | Protocol |
|-------------|-----------------|----------|
| BME280 SDA  | GPIO21          | I2C      |
| BME280 SCL  | GPIO22          | I2C      |
| GPS TX      | GPIO16 (RX2)    | UART2    |
| GPS RX      | GPIO17 (TX2)    | UART2    |
| Battery ADC | GPIO34          | ADC      |

## Firmware Architecture

ESP-IDF framework, written in C. Deep sleep wake cycle every 5 minutes.

### Wake Cycle

1. Wake from deep sleep
2. Read BME280 (temp, humidity, pressure)
3. Read GPS (up to 30s timeout, use last known position if no fix)
4. Read battery voltage via ADC
5. Format JSON payload
6. Connect WiFi, connect MQTT, publish
7. On failure: write to SPIFFS ring buffer
8. If buffer has pending readings: batch-send up to 10
9. Disconnect, enter deep sleep (5 min)

### Modules

| File             | Responsibility                              |
|-----------------|---------------------------------------------|
| main.c          | Entry point, wake cycle orchestration       |
| config.h        | WiFi, MQTT, pin, interval configuration     |
| sensor_bme280.c | BME280 I2C init and read                    |
| sensor_gps.c    | UART2 GPS init, NMEA parsing (GGA/RMC)     |
| mqtt_client.c   | WiFi connect, MQTT publish, disconnect      |
| data_buffer.c   | SPIFFS ring buffer for offline storage      |
| battery.c       | ADC battery voltage read and percentage     |

### JSON Payload

```json
{
  "device_id": "weatherprobe-01",
  "ts": 1709380800,
  "temp_c": 22.5,
  "humidity_pct": 45.2,
  "pressure_hpa": 1013.25,
  "lat": 60.1699,
  "lon": 24.9384,
  "alt_m": 15.3,
  "battery_pct": 78,
  "battery_v": 3.82,
  "gps_fix": true
}
```

## Cloud Infrastructure

### Data Path

```
ESP32 --MQTT/TLS--> HiveMQ Cloud --subscription--> GCP Cloud Function --publish--> Pub/Sub topic
```

- **MQTT Broker:** HiveMQ Cloud (free tier). Topic: `weatherprobe/{device_id}/data`
- **Bridge:** GCP Cloud Function (Python) subscribes to HiveMQ, publishes to Pub/Sub
- **Pub/Sub Topic:** `weatherprobe-readings`

### Security

- MQTT: TLS + username/password
- Cloud Function: GCP service account for Pub/Sub access
- Device credentials: stored in config.h (production: ESP-IDF NVS encrypted storage)

## Offline Handling

SPIFFS-based ring buffer on ESP32 internal flash. When MQTT publish fails, readings are queued. On next successful connection, up to 10 buffered readings are batch-sent before the current reading.

## Project Structure

```
ard_weatherprobe/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── config.h
│   ├── sensor_bme280.c/h
│   ├── sensor_gps.c/h
│   ├── mqtt_client.c/h
│   ├── data_buffer.c/h
│   └── battery.c/h
├── components/
│   └── bme280-driver/
├── cloud/
│   ├── cloud_function/
│   │   ├── main.py
│   │   └── requirements.txt
│   └── pubsub_setup.sh
└── docs/
    └── plans/
```

## Build

ESP-IDF toolchain. Commands: `idf.py build`, `idf.py flash`, `idf.py monitor`.
