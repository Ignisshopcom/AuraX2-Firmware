#pragma once
#include <stdint.h>
#include "led_driver.h"

struct EffectColor { uint8_t r, g, b; };

struct EffectParams {
    uint8_t     effectId;       // 1=solid, 2=android
    uint16_t    speed;          // 10–1000, 100=normální
    uint8_t     dotSize;        // velikost tečky (android)
    uint8_t     paletteSize;    // 1–4
    EffectColor palette[4];
};

class IEffect {
public:
    virtual ~IEffect() = default;
    virtual void     reset(const EffectParams& p, uint16_t numLeds) = 0;
    virtual void     update(ILedDriver& leds, const EffectParams& p) = 0;
    virtual uint32_t intervalUs(const EffectParams& p) const = 0;
};
