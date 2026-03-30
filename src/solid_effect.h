#pragma once
#include "effect.h"
#include <stdlib.h>

class SolidEffect : public IEffect {
public:
    ~SolidEffect() { free(_buf); }

    void reset(const EffectParams& p, uint16_t numLeds) override {
        free(_buf);
        _numLeds = numLeds;
        _buf = (uint8_t*)malloc(numLeds * 4);
    }

    void update(ILedDriver& leds, const EffectParams& p) override {
        if (!_buf) return;
        const EffectColor& c = (p.paletteSize > 0) ? p.palette[0] : EffectColor{255, 0, 0};
        for (int i = 0; i < _numLeds; i++) {
            _buf[i*4]   = 0xFF;   // 0xE0 | 0x1F — max brightness
            _buf[i*4+1] = c.b;
            _buf[i*4+2] = c.g;
            _buf[i*4+3] = c.r;
        }
        leds.showColumnDirect(_buf, _numLeds);
    }

    uint32_t intervalUs(const EffectParams&) const override {
        return 200000;  // 5 Hz — refresh jen při změně parametrů
    }

private:
    uint16_t _numLeds = 0;
    uint8_t* _buf     = nullptr;
};
