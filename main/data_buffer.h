#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

void buffer_init(void);
bool buffer_write(const char *json, size_t len);
int  buffer_count(void);
bool buffer_peek(char *out, size_t out_size);
void buffer_pop(void);

#endif
