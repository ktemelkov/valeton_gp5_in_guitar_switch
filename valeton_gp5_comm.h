#ifndef VALETON_GP5_COMM_H
#define VALETON_GP5_COMM_H

#include <stdint.h>


uint8_t valeton_gp5_crc8(const uint8_t* sysex_data, uint32_t length);
int valeton_gp5_build_sysex(const uint8_t* buffer, int len, uint8_t type);
uint8_t* valeton_gp5_current_preset_request(int& out_size);

uint8_t valeton_gp5_decode_op(const uint8_t* buffer, int len);
uint8_t valeton_gp5_decode_preset_no(const uint8_t* buffer, int len);

int valeton_gp5_msg_offset(const uint8_t* buffer, int len);

#endif // VALETON_GP5_COMM_H