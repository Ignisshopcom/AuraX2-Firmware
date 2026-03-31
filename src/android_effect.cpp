#include "android_effect.h"
#include <stdlib.h>
#include <string.h>

AndroidEffect::~AndroidEffect() {
    free(_buf);
}

void AndroidEffect::reset(const EffectParams& p, uint16_t numLeds) {
    free(_buf);
    _numLeds = numLeds;
    _buf     = (uint8_t*)malloc(numLeds * 4);
    _pos     = 0;
    _width   = 2;
    _phase   = 0;
    _call    = 0;
}

void AndroidEffect::update(ILedDriver& leds, const EffectParams& p) {
    if (!_buf || _numLeds == 0) return;

    uint16_t maxWidth = p.dotSize < 1 ? 1 : (p.dotSize > _numLeds ? _numLeds : p.dotSize);

    // 1. Pozadí
    bool hasBg = (p.paletteSize >= 2);
    for (int i = 0; i < _numLeds; i++) {
        if (hasBg) {
            _buf[i*4]   = 0xFF;
            _buf[i*4+1] = p.palette[1].b;
            _buf[i*4+2] = p.palette[1].g;
            _buf[i*4+3] = p.palette[1].r;
        } else {
            _buf[i*4]   = 0xE0;   // brightness=0 = LED off
            _buf[i*4+1] = 0;
            _buf[i*4+2] = 0;
            _buf[i*4+3] = 0;
        }
    }

    // 2. Fáze
    if (_width > maxWidth) {
        _phase = 1;
    } else if (_width < 2) {
        _phase = 0;
    }

    // 3. Pohyb
    uint16_t a = _pos;
    if (_phase == 0) {                    // expanze
        if (_call % 3 == 1) { a++; }
        else { _width++; }
    } else {                              // kontrakce
        a++;
        if (_call % 3 != 1) { if (_width > 0) _width--; }
    }

    // 4. Wrap
    if (a >= _numLeds) a = 0;

    // 5. Nakreslit oblouk s wraparound
    const EffectColor& c = p.palette[0];
    if ((uint32_t)a + _width <= _numLeds) {
        for (uint16_t i = a; i < a + _width; i++) {
            _buf[i*4]   = 0xFF;
            _buf[i*4+1] = c.b;
            _buf[i*4+2] = c.g;
            _buf[i*4+3] = c.r;
        }
    } else {
        for (uint16_t i = a; i < _numLeds; i++) {
            _buf[i*4]   = 0xFF;
            _buf[i*4+1] = c.b;
            _buf[i*4+2] = c.g;
            _buf[i*4+3] = c.r;
        }
        uint16_t tail = _width - (_numLeds - a);
        for (uint16_t i = 0; i < tail; i++) {
            _buf[i*4]   = 0xFF;
            _buf[i*4+1] = c.b;
            _buf[i*4+2] = c.g;
            _buf[i*4+3] = c.r;
        }
    }

    _pos = a;
    _call++;

    leds.showColumnDirect(_buf, _numLeds);
}

uint32_t AndroidEffect::intervalUs(const EffectParams& p) const {
    return 5000000UL / (p.speed > 0 ? p.speed : 1);
}
