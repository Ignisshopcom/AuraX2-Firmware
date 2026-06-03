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
    uint16_t _pos     = 0;    // pozice začátku oblouku
    uint16_t _width   = 2;    // aktuální šířka oblouku
    uint8_t  _phase   = 0;    // 0=expanze, 1=kontrakce
    uint32_t _call    = 0;    // čítač volání pro _call%3 timing
    uint8_t* _buf     = nullptr;
};
