/*
 * data_buffer.c -- SPIFFS ring buffer for offline sensor readings.
 *
 * Storage format (buffer.dat):
 *   [ uint16_t length ][ JSON payload bytes ] ...
 *
 * buffer_write() appends to the file.  If BUFFER_MAX_ENTRIES is
 * reached the oldest entry is popped first.
 *
 * buffer_pop() removes the first entry by reading the remaining
 * bytes into a heap buffer and rewriting the file.  This is
 * acceptable because at most BUFFER_MAX_ENTRIES (~50 KB) are
 * stored and the operation runs only a few times per wake cycle.
 */

#include "data_buffer.h"
#include "config.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "BUFFER";

void buffer_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_PARTITION,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    size_t total, used;
    esp_spiffs_info(SPIFFS_PARTITION, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", used, total);
}

bool buffer_write(const char *json, size_t len)
{
    /* Enforce the maximum entry count by dropping the oldest */
    if (buffer_count() >= BUFFER_MAX_ENTRIES) {
        ESP_LOGW(TAG, "Buffer full, dropping oldest");
        buffer_pop();
    }

    FILE *f = fopen(BUFFER_FILE, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open buffer for append");
        return false;
    }

    if (len > UINT16_MAX) {
        ESP_LOGE(TAG, "Entry too large: %d", (int)len);
        return false;
    }

    /* Write length-prefixed record */
    uint16_t slen = (uint16_t)len;
    fwrite(&slen, sizeof(slen), 1, f);
    fwrite(json, 1, len, f);
    fclose(f);

    ESP_LOGI(TAG, "Buffered %d bytes (%d entries)", (int)len, buffer_count());
    return true;
}

int buffer_count(void)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return 0;

    int count = 0;
    uint16_t slen;
    while (fread(&slen, sizeof(slen), 1, f) == 1) {
        fseek(f, slen, SEEK_CUR);
        count++;
    }
    fclose(f);
    return count;
}

bool buffer_peek(char *out, size_t out_size)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return false;

    uint16_t slen;
    if (fread(&slen, sizeof(slen), 1, f) != 1) {
        fclose(f);
        return false;
    }
    if (slen >= out_size) {
        fclose(f);
        return false;
    }
    fread(out, 1, slen, f);
    out[slen] = '\0';
    fclose(f);
    return true;
}

/*
 * buffer_pop -- Remove the first entry by rewriting the file with
 * everything after it.  If only one entry exists the file is deleted.
 */
void buffer_pop(void)
{
    FILE *f = fopen(BUFFER_FILE, "rb");
    if (!f) return;

    /* Skip past the first record */
    uint16_t slen;
    if (fread(&slen, sizeof(slen), 1, f) != 1) {
        fclose(f);
        remove(BUFFER_FILE);
        return;
    }
    fseek(f, slen, SEEK_CUR);

    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    long remaining = end - pos;

    if (remaining <= 0) {
        fclose(f);
        remove(BUFFER_FILE);
        return;
    }

    /* Copy the tail into a temporary heap buffer and rewrite */
    char *tmp = malloc(remaining);
    if (!tmp) {
        fclose(f);
        return;
    }
    fseek(f, pos, SEEK_SET);
    fread(tmp, 1, remaining, f);
    fclose(f);

    f = fopen(BUFFER_FILE, "wb");
    if (f) {
        fwrite(tmp, 1, remaining, f);
        fclose(f);
    }
    free(tmp);
}

void buffer_deinit(void)
{
    esp_vfs_spiffs_unregister(SPIFFS_PARTITION);
}
