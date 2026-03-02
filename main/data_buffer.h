/*
 * data_buffer.h -- SPIFFS-backed FIFO ring buffer for offline readings.
 *
 * When MQTT publishing fails, JSON payloads are appended to a flat
 * file on SPIFFS.  Each entry is stored as a 2-byte little-endian
 * length prefix followed by the raw JSON bytes.  On the next
 * successful connection the buffer is drained up to BATCH_SEND_MAX
 * entries at a time.  When the buffer reaches BUFFER_MAX_ENTRIES the
 * oldest entry is silently dropped.
 */

#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

/* Mount the SPIFFS partition (formats on first use) */
void buffer_init(void);

/* Append a JSON payload; drops the oldest entry if the buffer is full */
bool buffer_write(const char *json, size_t len);

/* Return the number of entries currently stored */
int  buffer_count(void);

/* Copy the oldest entry into `out` without removing it */
bool buffer_peek(char *out, size_t out_size);

/* Remove the oldest entry from the buffer file */
void buffer_pop(void);

/* Unmount the SPIFFS partition */
void buffer_deinit(void);

#endif /* DATA_BUFFER_H */
