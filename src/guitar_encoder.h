#ifndef GUITAR_ENCODER_H
#define GUITAR_ENCODER_H

#include <Arduino.h>
#include <stdint.h>
#include "digital.h"
#include "debug.h"


/**
 *
 */
class GuitarEncoder
{
public:
    GuitarEncoder(uint8_t pin1, uint8_t pin2, uint8_t btnPin)
        : _pin1(pin1), _pin2(pin2), _btnPin(btnPin), _hist1(0), _hist2(0), _histBtn(0), _lastPollTime(0) {}

    void begin()
    {
        // pinMode(_pin1, INPUT_PULLUP);
        // pinMode(_pin2, INPUT_PULLUP);
        pinMode(_btnPin, INPUT_PULLUP);

        _lastPollTime = millis();
    }

    void update()
    {
        // Update history buffers
        time_t now = millis();

        if (now - _lastPollTime >= 20)
        {
            _lastPollTime = now;
            ContactState btnState;

            _btnEvent = updateEncoderContact(_btnPin, _histBtn, btnState);

            // updateEncoderContact(_pin1, _hist1, _contactState1);
            
            // if (updateEncoderContact(_pin2, _hist2, _contactState2) == PRESSED)
            // {
            //     // Handle rotation event here if needed
            //     if (_contactState1 == CLOSED)
            //     {
            //         DEBUG_MSG("%s", "Encoder rotated clockwise.\n");
            //     }
            //     else
            //     {
            //         DEBUG_MSG("%s", "Encoder rotated counter-clockwise.\n");
            //     }
            // }
        }
    }

    bool hasButtonBeenPressed()
    {
        ContactEvent btnEvent = _btnEvent;
        _btnEvent = NO_CHANGE;
        return btnEvent == PRESSED;
    }

private:
    ContactEvent updateEncoderContact(uint8_t pin, uint8_t &hist, ContactState &contactState)
    {
        hist = (hist << 1) | !digitalRead(pin);

        if ((hist & 0b11000111) == 0b00000111)
        {
            hist = 0xFF;
            contactState = CLOSED;
            return PRESSED;
        }
        
        if ((hist & 0b11000111) == 0b11000000)
        {
            hist = 0x00;
            contactState = OPENED;
            return RELEASED;
        }

        return NO_CHANGE;
    }

private:
    uint8_t _pin1;
    uint8_t _pin2;
    uint8_t _btnPin;

    uint8_t _hist1;
    uint8_t _hist2;
    uint8_t _histBtn;
    
    ContactEvent _btnEvent;

    ContactState _contactState1;
    ContactState _contactState2;

    time_t _lastPollTime;
};

#endif // GUITAR_ENCODER_H