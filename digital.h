#ifndef DIGITAL_H
#define DIGITAL_H

#include <Arduino.h>
#include <stdint.h>

enum ContactState
{
    OPENED = 0,
    CLOSED = 1
};

enum ContactEvent
{
    NO_CHANGE = 0,
    PRESSED = 1,
    RELEASED = 2
};

#endif // DIGITAL_H