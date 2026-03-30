#pragma once
#include "effect.h"

class AndroidEffect : public IEffect {
public:
    ~AndroidEffect();
    void     reset(const EffectParams& p, uint16_t numLeds) override;
    void     update(ILedDriver& leds, const EffectParams& p) override;
    uint32_t intervalUs(const EffectParams& p) const override;

private:
    uint16_t _numLeds  = 0;
    int      _pos      = 0;    // pozice začátku tečky
    int      _dir      = 1;    // směr pohybu: +1 nebo -1
    uint8_t  _colorIdx = 0;    // index aktuální barvy v paletě
    uint8_t* _buf      = nullptr;
};
