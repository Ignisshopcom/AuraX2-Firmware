#include "android_effect.h"
#include <stdlib.h>
#include <string.h>

AndroidEffect::~AndroidEffect() {
    free(_buf);
}

void AndroidEffect::reset(const EffectParams&, uint16_t numLeds) {
    free(_buf);
    _numLeds = numLeds;
    _buf     = (uint8_t*)malloc((size_t)numLeds * 4);
    _posQ8   = 0;
    _widthQ8 = 2u << 8;
    _growing = true;
}

void AndroidEffect::update(ILedDriver& leds, const EffectParams& p) {
    if (!_buf || _numLeds == 0) return;

    const bool hasBg = p.paletteSize >= 2;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t* dst = _buf + (size_t)i * 4;
        if (hasBg) {
            dst[0] = 0xFF;
            dst[1] = p.palette[1].b;
            dst[2] = p.palette[1].g;
            dst[3] = p.palette[1].r;
        } else {
            dst[0] = 0xE0;
            dst[1] = dst[2] = dst[3] = 0;
        }
    }

    uint16_t maxWidth = p.dotSize < 1 ? 1 : (p.dotSize > _numLeds ? _numLeds : p.dotSize);
    uint32_t minWidthQ8 = 1u << 8;
    uint32_t maxWidthQ8 = (uint32_t)maxWidth << 8;
    uint16_t uiSpeed = p.speed > 255 ? 255 : p.speed;
    uint16_t speed = (uint16_t)(((uint32_t)uiSpeed * 1000u + 127u) / 255u);
    uint32_t moveStepQ8 = 24 + (uint32_t)speed * 4;
    uint32_t widthStepQ8 = 8 + speed / 2;

    if (_growing) {
        _widthQ8 += widthStepQ8;
        if (_widthQ8 >= maxWidthQ8) {
            _widthQ8 = maxWidthQ8;
            _growing = false;
        }
    } else {
        _widthQ8 = (_widthQ8 > minWidthQ8 + widthStepQ8) ? (_widthQ8 - widthStepQ8) : minWidthQ8;
        if (_widthQ8 <= minWidthQ8) _growing = true;
    }

    uint32_t spanQ8 = (uint32_t)_numLeds << 8;
    _posQ8 = (_posQ8 + moveStepQ8) % spanQ8;

    const EffectColor& color = p.palette[0];
    const uint32_t edgeQ8 = 1u << 8;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint32_t pixelQ8 = (uint32_t)i << 8;
        uint32_t distQ8 = (pixelQ8 + spanQ8 - _posQ8) % spanQ8;
        if (distQ8 > _widthQ8 + edgeQ8) continue;

        uint8_t scale = 255;
        if (distQ8 < edgeQ8) {
            scale = (uint8_t)(distQ8 * 255 / edgeQ8);
        } else if (distQ8 > _widthQ8) {
            scale = (uint8_t)((_widthQ8 + edgeQ8 - distQ8) * 255 / edgeQ8);
        }

        uint8_t* dst = _buf + (size_t)i * 4;
        uint16_t b = dst[1] + ((uint16_t)color.b * scale / 255);
        uint16_t g = dst[2] + ((uint16_t)color.g * scale / 255);
        uint16_t r = dst[3] + ((uint16_t)color.r * scale / 255);
        dst[1] = b > 255 ? 255 : (uint8_t)b;
        dst[2] = g > 255 ? 255 : (uint8_t)g;
        dst[3] = r > 255 ? 255 : (uint8_t)r;
        dst[0] = (dst[1] || dst[2] || dst[3]) ? 0xFF : 0xE0;
    }

    leds.showColumnDirect(_buf, _numLeds);
}

uint32_t AndroidEffect::intervalUs(const EffectParams&) const {
    return 2000;
}
