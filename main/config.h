#ifndef CONFIG_H
#define CONFIG_H

// WiFi
#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASS           "YOUR_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS 15000

// MQTT
#define MQTT_BROKER_URI     "mqtts://broker.hivemq.com:8883"
#define MQTT_USERNAME       "your_user"
#define MQTT_PASSWORD       "your_pass"
#define MQTT_TOPIC          "weatherprobe/weatherprobe-01/data"
#define MQTT_CLIENT_ID      "weatherprobe-01"

// Device
#define DEVICE_ID           "weatherprobe-01"
#define SLEEP_DURATION_US   (5ULL * 60 * 1000000)  // 5 minutes

// BME280 (I2C)
#define BME280_I2C_PORT     I2C_NUM_0
#define BME280_I2C_SDA      GPIO_NUM_21
#define BME280_I2C_SCL      GPIO_NUM_22
#define BME280_I2C_ADDR     0x76
#define BME280_I2C_SPEED_HZ 400000

// GPS (UART2)
#define GPS_UART_PORT       UART_NUM_2
#define GPS_UART_TX         GPIO_NUM_17
#define GPS_UART_RX         GPIO_NUM_16
#define GPS_BAUD_RATE       9600
#define GPS_FIX_TIMEOUT_MS  30000
#define GPS_BUF_SIZE        512

// Battery ADC
#define BATTERY_ADC_GPIO    GPIO_NUM_34
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6
// Voltage divider: R1=100k, R2=100k -> factor = 2.0
#define BATTERY_DIVIDER     2.0f
#define BATTERY_FULL_V      4.2f
#define BATTERY_EMPTY_V     3.0f

// SPIFFS buffer
#define SPIFFS_BASE_PATH    "/spiffs"
#define SPIFFS_PARTITION    "storage"
#define BUFFER_FILE         "/spiffs/buffer.dat"
#define BUFFER_MAX_ENTRIES  100
#define BATCH_SEND_MAX      10

#endif
