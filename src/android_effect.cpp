#include "android_effect.h"
#include <stdlib.h>
#include <string.h>

AndroidEffect::~AndroidEffect() {
    free(_buf);
}

void AndroidEffect::reset(const EffectParams& p, uint16_t numLeds) {
    free(_buf);
    _numLeds  = numLeds;
    _buf      = (uint8_t*)malloc(numLeds * 4);
    _pos      = 0;
    _dir      = 1;
    _colorIdx = 0;
}

void AndroidEffect::update(ILedDriver& leds, const EffectParams& p) {
    if (!_buf || _numLeds == 0) return;

    uint8_t sz   = p.dotSize < 1 ? 1 : (p.dotSize > _numLeds ? _numLeds : p.dotSize);
    uint8_t pSz  = p.paletteSize > 0 ? p.paletteSize : 1;
    const EffectColor& c = p.palette[_colorIdx % pSz];

    // Pozadí: všechny LED zhasnuté
    for (int i = 0; i < _numLeds; i++) {
        _buf[i*4]   = 0xE0;   // brightness=0 = LED off
        _buf[i*4+1] = 0;
        _buf[i*4+2] = 0;
        _buf[i*4+3] = 0;
    }

    // Nakreslit tečku
    for (int i = 0; i < sz; i++) {
        int idx = _pos + i;
        if (idx >= 0 && idx < _numLeds) {
            _buf[idx*4]   = 0xFF;
            _buf[idx*4+1] = c.b;
            _buf[idx*4+2] = c.g;
            _buf[idx*4+3] = c.r;
        }
    }

    leds.showColumnDirect(_buf, _numLeds);

    // Posunout pozici
    _pos += _dir;
    int maxPos = (int)_numLeds - (int)sz;
    if (maxPos < 0) maxPos = 0;

    if (_pos >= maxPos) {
        _pos  = maxPos;
        _dir  = -1;
        _colorIdx = (_colorIdx + 1) % pSz;
    } else if (_pos <= 0) {
        _pos  = 0;
        _dir  = 1;
        _colorIdx = (_colorIdx + 1) % pSz;
    }
}

uint32_t AndroidEffect::intervalUs(const EffectParams& p) const {
    if (_numLeds == 0 || p.speed == 0) return 100000;
    // 100_000_000 / (numLeds * speed) → při 144 LED, speed=100 ≈ 6944 µs/krok
    return 100000000UL / ((uint32_t)_numLeds * p.speed);
}
