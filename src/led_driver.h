#pragma once
#include <stdint.h>

// Abstract LED strip driver interface.
// Both APA102 (SPI) and WS281x (RMT) implement this.
class ILedDriver {
public:
    virtual ~ILedDriver() = default;
    virtual uint16_t numLeds() const = 0;
    virtual uint16_t physicalNumLeds() const { return numLeds(); }
    virtual uint16_t logicalNumLeds() const { return numLeds(); }
    virtual uint16_t maxRefreshHz() const { return 2500; }
    // pixData: [0xE0|brightness, B, G, R] per LED — .pix wire format.
    // count: number of LEDs in pixData (clamped to numLeds internally).
    virtual void showColumnDirect(const uint8_t* pixData, uint16_t count) = 0;
    virtual void clear() = 0;  // turn off all LEDs immediately

    // Global brightness override: 0 = use per-pixel brightness from .pix file (default),
    // 1–100 = override all pixels to this percentage.
    virtual void setBrightness(uint8_t pct) { (void)pct; }
    virtual void setReverse(bool reverse) { (void)reverse; }
    virtual void setMirror(bool mirror) { (void)mirror; }
    virtual void setContactPoi(bool enabled) { (void)enabled; }

    // Current limit: mALimit = max total draw in mA (0 = disabled).
    // mAPerLed = estimated mA per LED at full white (R=G=B=255); typical 60 mA.
    virtual void setCurrentLimit(uint16_t mALimit, uint16_t mAPerLed) { (void)mALimit; (void)mAPerLed; }
};
