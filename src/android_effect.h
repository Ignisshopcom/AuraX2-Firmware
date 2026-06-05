#pragma once
#include "effect.h"

class AndroidEffect : public IEffect {
public:
    ~AndroidEffect();
    void     reset(const EffectParams& p, uint16_t numLeds) override;
    void     update(ILedDriver& leds, const EffectParams& p) override;
    uint32_t intervalUs(const EffectParams& p) const override;

private:
    uint16_t _numLeds = 0;
    uint32_t _posQ8   = 0;
    uint32_t _widthQ8 = 2u << 8;
    bool     _growing = true;
    uint8_t* _buf     = nullptr;
};
