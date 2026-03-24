#pragma once
#include <stdint.h>

// Abstract LED strip driver interface.
// Both APA102 (SPI) and WS281x (RMT) implement this.
class ILedDriver {
public:
    virtual ~ILedDriver() = default;
    virtual uint16_t numLeds() const = 0;
    // pixData: [0xE0|brightness, B, G, R] per LED — .pix wire format.
    // count: number of LEDs in pixData (clamped to numLeds internally).
    virtual void showColumnDirect(const uint8_t* pixData, uint16_t count) = 0;
};
