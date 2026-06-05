#pragma once
#include <stdint.h>
#include "led_driver.h"

struct EffectColor { uint8_t r, g, b; };

struct EffectParams {
    uint8_t     effectId;       // 1=solid, 2=android, 10+ = AuraX/WLED-like effects
    uint16_t    speed;          // 0-255 UI value, mapped internally to legacy 0-1000
    uint8_t     intensity;      // 0-255, effect-specific strength
    uint8_t     dotSize;        // effect-specific size/width
    uint8_t     paletteId;      // 0=custom color slots, 1+ = built-in palette
    uint8_t     paletteSize;    // 1-4 color slots
    uint8_t     reverse;        // 1 = render from the opposite LED end
    EffectColor palette[4];
};

class IEffect {
public:
    virtual ~IEffect() = default;
    virtual void     reset(const EffectParams& p, uint16_t numLeds) = 0;
    virtual void     update(ILedDriver& leds, const EffectParams& p) = 0;
    virtual uint32_t intervalUs(const EffectParams& p) const = 0;
};
