/*
 * sensor_gps.c -- NMEA GGA parser for a UART-connected GPS receiver.
 *
 * The driver installs a UART peripheral, then reads bytes in a loop
 * assembling NMEA sentences.  Each complete sentence is validated with
 * an XOR checksum and, if it is a GGA message with a valid fix, the
 * latitude/longitude/altitude fields are parsed and returned.
 *
 * Coordinates arrive in DDMM.MMMM format and are converted to signed
 * decimal degrees.  The driver deletes the UART when it is done to
 * save power.
 */

#include "sensor_gps.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "GPS";

/*
 * parse_ddmm -- Convert NMEA DDMM.MMMM to decimal degrees.
 * E.g. "6010.1940" => 60 + 10.1940/60 = 60.16990
 */
static double parse_ddmm(const char *s)
{
    if (!s || !*s) return 0.0;
    double raw = atof(s);
    int deg = (int)(raw / 100);
    return deg + (raw - deg * 100) / 60.0;
}

/*
 * nmea_checksum_ok -- Verify the XOR checksum between '$' and '*'.
 * Returns true only if the computed checksum matches the two hex
 * digits following the asterisk.
 */
static bool nmea_checksum_ok(const char *sentence)
{
    if (sentence[0] != '$') return false;
    const char *p = sentence + 1;
    uint8_t crc = 0;
    while (*p && *p != '*') crc ^= (uint8_t)*p++;
    if (*p != '*') return false;
    if (p[1] == '\0' || p[2] == '\0') return false;
    char hex[3] = {p[1], p[2], 0};
    return crc == (uint8_t)strtol(hex, NULL, 16);
}

/*
 * parse_gga -- Extract fields from a GGA sentence (checksum already
 * stripped).  Populates the reading struct and returns true on success.
 *
 * GGA field order: talker+id, time, lat, N/S, lon, E/W, quality,
 *                  sats, HDOP, alt, alt-unit, geoid-sep, ...
 */
static bool parse_gga(char *s, gps_reading_t *out)
{
    char *fields[15];
    int n = 0;
    for (char *tok = strtok(s, ","); tok && n < 15; tok = strtok(NULL, ","))
        fields[n++] = tok;
    if (n < 10) return false;

    /* UTC time -- HHMMSS.ss */
    double t = atof(fields[1]);
    out->hour = (uint8_t)(t / 10000);
    out->min = (uint8_t)((int)t % 10000 / 100);
    out->sec = (uint8_t)((int)t % 100);

    /* Fix quality: 0 = no fix, 1 = GPS, 2 = DGPS, ... */
    int fix_quality = atoi(fields[6]);
    out->fix_valid = fix_quality > 0;
    out->satellites = (uint8_t)atoi(fields[7]);

    /* Latitude / longitude in DDMM.MMMM, with N/S and E/W indicators */
    out->latitude = parse_ddmm(fields[2]);
    if (n > 3 && fields[3][0] == 'S') out->latitude = -out->latitude;
    out->longitude = parse_ddmm(fields[4]);
    if (n > 5 && fields[5][0] == 'W') out->longitude = -out->longitude;

    out->altitude_m = (float)atof(fields[9]);
    return true;
}

/* ---- Public API ---- */

void gps_init(void)
{
    uart_config_t cfg = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_PORT, GPS_UART_TX, GPS_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_PORT, GPS_BUF_SIZE * 2,
                                        0, 0, NULL, 0));
    ESP_LOGI(TAG, "Initialized on UART%d", GPS_UART_PORT);
}

gps_reading_t gps_read(void)
{
    gps_reading_t result = {0};
    uint8_t buf[GPS_BUF_SIZE];
    char line[GPS_BUF_SIZE];
    int line_pos = 0;

    int64_t start = esp_timer_get_time();
    int64_t timeout = (int64_t)GPS_FIX_TIMEOUT_MS * 1000;

    while ((esp_timer_get_time() - start) < timeout) {
        int len = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        /* Assemble NMEA sentences character by character */
        for (int i = 0; i < len; i++) {
            char c = (char)buf[i];
            if (c == '$') line_pos = 0;           /* start of new sentence */
            if (line_pos < GPS_BUF_SIZE - 1) {
                line[line_pos++] = c;
            }
            if (c == '\n' && line_pos > 6) {
                line[line_pos] = '\0';

                if (!nmea_checksum_ok(line)) {
                    line_pos = 0;
                    continue;
                }

                /* Only process GGA sentences (GPS or multi-constellation) */
                if (strncmp(line, "$GPGGA", 6) == 0 ||
                    strncmp(line, "$GNGGA", 6) == 0) {
                    char tmp[GPS_BUF_SIZE];
                    strncpy(tmp, line, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    char *star = strchr(tmp, '*');
                    if (star) *star = '\0';

                    if (parse_gga(tmp, &result) && result.fix_valid) {
                        ESP_LOGI(TAG, "Fix: lat=%.6f lon=%.6f alt=%.1fm sats=%d",
                                 result.latitude, result.longitude,
                                 result.altitude_m, result.satellites);
                        uart_driver_delete(GPS_UART_PORT);
                        return result;
                    }
                }
                line_pos = 0;
            }
        }
    }

    ESP_LOGW(TAG, "No fix within %d ms", GPS_FIX_TIMEOUT_MS);
    uart_driver_delete(GPS_UART_PORT);
    return result;
}
