// CRC16 Implementation based on work by Francesco Sacchi

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define CRC16_INIT_VAL ((uint16_t)0x0000)

extern const uint16_t crc16_table[256];

inline uint16_t update_crc16(uint8_t c, uint16_t prev_crc) {
    return (prev_crc >> 8) ^ pgm_read_word(&crc16_table[(prev_crc ^ c) & 0xff]);
}


#endif
