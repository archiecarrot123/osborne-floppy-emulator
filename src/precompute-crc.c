#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CRC_POLYNOMIAL 0x1021 // CRC-16-CCITT


static inline uint32_t software_crc_step(uint32_t buffer, uint8_t byte) {
  buffer |= byte;
  for (unsigned int j = 0; j < 8; j++) {
    buffer <<= 1;
    if (buffer & 0x01000000) {
      buffer ^= CRC_POLYNOMIAL << 8;
    }
  }
  return buffer;
}

static uint16_t software_crc_partial(const uint8_t *data, size_t length, uint16_t crc) {
  uint32_t buffer = crc << 8;
  for (unsigned int i = 0; i < length; i++) {
    buffer = software_crc_step(buffer, data[i]);
  }
  return buffer >> 8;
}

static uint16_t software_crc(const uint8_t *data, size_t length, uint16_t crc) {
  uint32_t buffer = crc << 8;
  for (unsigned int i = 0; i < length; i++) {
    buffer = software_crc_step(buffer, data[i]);
  }
  for (unsigned int i = 0; i < 2; i++) {
    buffer = software_crc_step(buffer, 0x00);
  }
  return buffer >> 8;
}

int main(int argc, const char *argv[]) {
  uint8_t data[16];
  for (unsigned int i = 1; i < argc; i++) {
    data[i-1] = strtol(argv[i], NULL, 16);
  }
  printf("result: 0x%04X\n", software_crc(data, argc - 1, 0xFFFF));
  printf("partial result: 0x%04X\n", software_crc_partial(data, argc - 1, 0xFFFF));
  return 0;
}
