#ifndef DEBUG_H
#define DEBUG_H


#define ENABLE_DEBUG_MESSAGES


#ifdef ENABLE_DEBUG_MESSAGES
#define DEBUG_MSG(format, ...) Serial.printf(format, __VA_ARGS__)
#define DEBUG_BUFFER(buffer, len) printHexBuffer(buffer, len)

/**
 *
 */
static void printHexBuffer(const uint8_t *buffer, int len)
{
  Serial.print("Data: ");

  for (int i = 0; i < len; i++)
  {
    Serial.printf("%02X ", buffer[i]);
  }

  Serial.println();
}

#else
#define DEBUG_MSG(format, ...)
#define DEBUG_BUFFER(buffer, len)
#endif

#endif