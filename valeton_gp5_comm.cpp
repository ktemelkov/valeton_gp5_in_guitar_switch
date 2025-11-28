#include <stdint.h>
#include <string.h>
#include "valeton_gp5_comm.h"

#define VALETON_GP5_TX_TEMP_BUFFER_SIZE 1024

static uint8_t TransmitBuffer[VALETON_GP5_TX_TEMP_BUFFER_SIZE];

uint8_t valeton_gp5_crc8(const uint8_t* sysex_data, uint32_t length) 
{
    uint8_t raw_data[64];
    uint32_t raw_length = length - 4; // exclude F0, CRC(2 bytes), F7
    uint32_t nibble_count;
    uint8_t crc;
    uint8_t high_nibble;
    uint8_t low_nibble;
    uint8_t* sys_ptr = (uint8_t*)sysex_data;

    // skip 0xF0 and 2 byte CRC
    sys_ptr += 3;

    nibble_count = raw_length / 2;

    // Pack low nibbles into raw data bytes
    for (uint8_t i = 0; i < nibble_count; i++) 
    {
        high_nibble = *sys_ptr & 0x0F; // Low nibble of first byte
        sys_ptr++;
        
        low_nibble = *sys_ptr & 0x0F; // Low nibble of second byte
        sys_ptr++;

        raw_data[i] = (high_nibble << 4) | low_nibble;
    }

    // CRC-8 calculation (polynomial 0x07, initial value 0x00)
    crc = 0;
    for (uint8_t i = 0; i < nibble_count; i++) 
    {
        crc ^= raw_data[i];
        
        for (uint8_t j = 0; j < 8; j++) 
        {
            if (crc & 0x80) 
            {
                crc = ((crc << 1) & 0xFF) ^ 0x07;
            } 
            else 
            {
                crc = (crc << 1) & 0xFF;
            }
        }
    }

    return crc;
}


int valeton_gp5_build_sysex(const uint8_t* buffer, int len, uint8_t type) 
{
    int write_index = 0;
    uint8_t crc;
    int packet_size;

    memset((void*)TransmitBuffer, 0, VALETON_GP5_TX_TEMP_BUFFER_SIZE);

    // set start marker
    TransmitBuffer[write_index++] = 0x80;
    TransmitBuffer[write_index++] = 0x80;
    TransmitBuffer[write_index++] = 0xF0;

    // next 2 bytes are Crc, skip for now
    write_index += 2;

    // next 4 bytes are vendor ID??
    TransmitBuffer[write_index++] = 0x00;
    TransmitBuffer[write_index++] = 0x01;
    TransmitBuffer[write_index++] = 0x00;
    TransmitBuffer[write_index++] = 0x00;

    // length bytes. Value sent as e.g. 0x14 as 01 04
    TransmitBuffer[write_index++] = len >> 4;
    TransmitBuffer[write_index++] = len & 0x0F;

    // 0101 for messages sent to GP5, 0102 for receive??
    // or could be 0101 for requests, 0102 for response and 0104 for status?
    TransmitBuffer[write_index++] = 0x01;
    TransmitBuffer[write_index++] = type;

    if (write_index + len + 1 > VALETON_GP5_TX_TEMP_BUFFER_SIZE) 
    {
        // not enough space in buffer
        return 0;
    } 

    // copy the data payload, write_index
    // now starts the data payload
    memcpy((void*)&TransmitBuffer[write_index], (void*)buffer, len);
    write_index += len;

    // end packet marker
    TransmitBuffer[write_index++] = 0xF7;

    // save the packet size
    packet_size = write_index;

    // calc Crc before Midi USB encoding
    crc = valeton_gp5_crc8(TransmitBuffer + 2, packet_size - 2); // skip initial 0x80,0x80

    // Crc value e.g. 0x63 becomes 06,03
    TransmitBuffer[3] = crc >> 4;
    TransmitBuffer[4] = crc & 0x0F;

    return packet_size;
}


uint8_t* valeton_gp5_current_preset_request(int& out_size)
{
    uint8_t req[] = { 0x04, 0x03 }; 
    out_size = valeton_gp5_build_sysex((const uint8_t*)req, sizeof(req), 0x02);

    return TransmitBuffer;
}

int valeton_gp5_msg_offset(const uint8_t* buffer, int len)
{
    // Find the start of the SysEx message in the buffer
    for (int i = 0; i < len; i++) 
    {
        if (buffer[i] == 0xF0) 
        {
            return i;
        }
    }

    return -1; // Not found
}

uint8_t valeton_gp5_decode_op(const uint8_t* buffer, int len) 
{
    int offset = valeton_gp5_msg_offset(buffer, len);
    if (offset < 0 || (offset + 12) >= len) 
    {
        return 0xFF; // Invalid
    }

    // Operation type is at offset + 10 and +11
    uint8_t op_type_high = buffer[offset + 11] & 0x0F;
    uint8_t op_type_low  = buffer[offset + 12] & 0x0F;

    return (op_type_high << 4) | op_type_low;
}

uint8_t valeton_gp5_decode_preset_no(const uint8_t* buffer, int len) 
{
    int offset = valeton_gp5_msg_offset(buffer, len);
    if (offset < 0 || (offset + 14) >= len) 
    {
        return 0xFF; // Invalid
    }

    // Preset number is at offset + 13 and +14
    uint8_t preset_no_high = buffer[offset + 13] & 0x0F;
    uint8_t preset_no_low  = buffer[offset + 14] & 0x0F;

    return (preset_no_high << 4) | preset_no_low;
}
