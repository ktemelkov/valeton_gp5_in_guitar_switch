#ifndef GUITAR_SWITCH_H
#define GUITAR_SWITCH_H

#include <Arduino.h>
#include <stdint.h>
#include "digital.h"
#include "debug.h"

/**
 *
 */
enum SwitchPosition
{
    TOP = 0,
    MIDDLE = 1,
    BOTTOM = 2
};


/**
 *
 */
class GuitarSwitch
{
public:
    GuitarSwitch(uint8_t pin1, uint8_t pin2)
        : _pin1(pin1), _pin2(pin2), _hist1(0), _hist2(0), _lastPollTime(0), _contactState1(OPENED), _contactState2(OPENED) {}

    void begin()
    {
        pinMode(_pin1, INPUT_PULLUP);
        pinMode(_pin2, INPUT_PULLUP);

        _contactState1 = digitalRead(_pin1) == LOW ? CLOSED : OPENED;
        _contactState2 = digitalRead(_pin2) == LOW ? CLOSED : OPENED;

        _hist1 = _contactState1 == CLOSED ? 0xFF : 0x00;
        _hist2 = _contactState2 == CLOSED ? 0xFF : 0x00;
        _lastPollTime = millis();
    }

    void update()
    {
        // Update history buffers
        static time_t lastPollTime = 0;
        time_t now = millis();

        if (now - _lastPollTime >= 20)
        {
            _lastPollTime = now;
            updateSwitchContact(_pin1, _hist1, _contactState1);
            updateSwitchContact(_pin2, _hist2, _contactState2);
        }
    }

    SwitchPosition getPosition() const
    {
        if (_contactState1 == CLOSED && _contactState2 == OPENED)
        {
            return TOP;
        }
        else if (_contactState1 == OPENED && _contactState2 == CLOSED)
        {
            return BOTTOM;
        }
        else
        {
            return MIDDLE;
        }
    }

private:
    void updateSwitchContact(uint8_t pin, uint8_t &hist, ContactState &contactState)
    {
        hist = (hist << 1) | !digitalRead(pin);

        if ((hist & 0b11000111) == 0b00000111)
        {
            hist = 0xFF;
            contactState = CLOSED;

            DEBUG_MSG("Switch on pin %d CLOSED\n", pin);
        }
        else if ((hist & 0b11000111) == 0b11000000)
        {
            hist = 0x00;
            contactState = OPENED;

            DEBUG_MSG("Switch on pin %d OPENED\n", pin);
        }
    }

private:
    uint8_t _pin1;
    uint8_t _pin2;
    uint8_t _hist1;
    uint8_t _hist2;    
    time_t _lastPollTime;
    ContactState _contactState1;
    ContactState _contactState2;
};

#endif // GUITAR_SWITCH_H