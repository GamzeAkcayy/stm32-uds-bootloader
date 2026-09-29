#ifndef CRC32_H
#define CRC32_H
#include <stdint.h>

uint32_t crc32_calc(const uint8_t *data, uint32_t len);

#endif
