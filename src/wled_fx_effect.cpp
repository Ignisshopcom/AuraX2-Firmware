#include "wled_fx_effect.h"
#include <stdlib.h>
#include <string.h>

WledFxEffect::~WledFxEffect() {
    free(_buf);
}

void WledFxEffect::reset(const EffectParams&, uint16_t numLeds) {
    free(_buf);
    _numLeds = numLeds;
    _buf = (uint8_t*)malloc((size_t)numLeds * 4);
    _phase = 0;
    _frame = 0;
    _rng = 0x1234abcd ^ ((uint32_t)numLeds << 16);
    _rippleOrigin = numLeds ? numLeds / 2 : 0;
    _rippleRadius = 0;
    clear();
}

void WledFxEffect::update(ILedDriver& leds, const EffectParams& p) {
    if (!_buf || _numLeds == 0) return;

    switch (p.effectId) {
        case EFFECT_BPM:        renderBpm(p); break;
        case EFFECT_FLOW:       renderFlow(p); break;
        case EFFECT_GRAVCENTER: renderGravcenter(p); break;
        case EFFECT_GRAVFREQ:   renderGravfreq(p); break;
        case EFFECT_CHASE2:     renderChase(p, 2); break;
        case EFFECT_CHASE3:     renderChase(p, 3); break;
        case EFFECT_CHUNCHUN:   renderChunchun(p); break;
        case EFFECT_LAKE:       renderLake(p); break;
        case EFFECT_METEOR:     renderMeteor(p); break;
        case EFFECT_NOISE3:     renderNoise3(p); break;
        case EFFECT_OSCILLATE:  renderOscillate(p); break;
        case EFFECT_RIPPLE:     renderRipple(p); break;
        case EFFECT_RUNNING:    renderRunning(p); break;
        case EFFECT_STROBE:     renderStrobe(p); break;
        case EFFECT_FADE:       renderFade(p); break;
        case EFFECT_RAINBOW:    renderRainbow(p); break;
        default:                renderFlow(p); break;
    }

    show(leds);
    _phase += speedStep(p);
    _frame++;
}

uint32_t WledFxEffect::intervalUs(const EffectParams&) const {
    return 25000;  // 40 FPS: smooth enough, still safe for WS281x on typical strip lengths.
}

void WledFxEffect::clear() {
    if (!_buf) return;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t* p = _buf + (size_t)i * 4;
        p[0] = 0xE0;
        p[1] = p[2] = p[3] = 0;
    }
}

void WledFxEffect::fade(uint8_t keep) {
    if (!_buf) return;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t* p = _buf + (size_t)i * 4;
        p[1] = (uint8_t)((uint16_t)p[1] * keep / 255);
        p[2] = (uint8_t)((uint16_t)p[2] * keep / 255);
        p[3] = (uint8_t)((uint16_t)p[3] * keep / 255);
        p[0] = (p[1] || p[2] || p[3]) ? 0xFF : 0xE0;
    }
}

void WledFxEffect::setPixel(uint16_t i, const EffectColor& c, uint8_t scale) {
    if (i >= _numLeds) return;
    uint8_t* p = _buf + (size_t)i * 4;
    p[1] = (uint8_t)((uint16_t)c.b * scale / 255);
    p[2] = (uint8_t)((uint16_t)c.g * scale / 255);
    p[3] = (uint8_t)((uint16_t)c.r * scale / 255);
    p[0] = (p[1] || p[2] || p[3]) ? 0xFF : 0xE0;
}

void WledFxEffect::addPixel(uint16_t i, const EffectColor& c, uint8_t scale) {
    if (i >= _numLeds) return;
    uint8_t* p = _buf + (size_t)i * 4;
    uint16_t b = p[1] + ((uint16_t)c.b * scale / 255);
    uint16_t g = p[2] + ((uint16_t)c.g * scale / 255);
    uint16_t r = p[3] + ((uint16_t)c.r * scale / 255);
    p[1] = b > 255 ? 255 : (uint8_t)b;
    p[2] = g > 255 ? 255 : (uint8_t)g;
    p[3] = r > 255 ? 255 : (uint8_t)r;
    p[0] = (p[1] || p[2] || p[3]) ? 0xFF : 0xE0;
}

void WledFxEffect::drawBlob(uint16_t center, uint16_t width, const EffectColor& c, uint8_t scale) {
    if (width < 1) width = 1;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t d = circularDistance(i, center);
        if (d > width) continue;
        uint8_t v = (uint8_t)((uint32_t)(width - d + 1) * scale / (width + 1));
        addPixel(i, c, v);
    }
}

void WledFxEffect::show(ILedDriver& leds) {
    leds.showColumnDirect(_buf, _numLeds);
}

uint16_t WledFxEffect::speedStep(const EffectParams& p) const {
    uint16_t s = p.speed < 10 ? 10 : (p.speed > 1000 ? 1000 : p.speed);
    return 1 + s / 7;
}

uint8_t WledFxEffect::intensity(const EffectParams& p) const {
    return p.intensity;
}

uint16_t WledFxEffect::sizeParam(const EffectParams& p, uint16_t fallback) const {
    uint16_t s = p.dotSize ? p.dotSize : fallback;
    if (s < 1) s = 1;
    if (s > _numLeds) s = _numLeds;
    return s;
}

uint16_t WledFxEffect::circularDistance(uint16_t a, uint16_t b) const {
    uint16_t d = (a > b) ? (a - b) : (b - a);
    uint16_t wrap = _numLeds - d;
    return d < wrap ? d : wrap;
}

uint8_t WledFxEffect::wave8(uint16_t x) const {
    uint8_t v = (uint8_t)x;
    return (v < 128) ? (uint8_t)(v << 1) : (uint8_t)(255 - ((v - 128) << 1));
}

uint8_t WledFxEffect::hash8(uint16_t x, uint16_t y) const {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + 0x9e3779b9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (uint8_t)(h >> 24);
}

uint8_t WledFxEffect::noise8(uint16_t x, uint16_t t) const {
    uint16_t cell = x >> 4;
    uint8_t frac = (uint8_t)((x & 0x0F) << 4);
    uint8_t a = hash8(cell, t >> 4);
    uint8_t b = hash8(cell + 1, t >> 4);
    return (uint8_t)(a + ((int16_t)b - a) * frac / 255);
}

uint16_t WledFxEffect::randomLed() {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _numLeds ? (uint16_t)(_rng % _numLeds) : 0;
}

EffectColor WledFxEffect::wheel(uint8_t pos) const {
    if (pos < 85) return EffectColor{(uint8_t)(255 - pos * 3), (uint8_t)(pos * 3), 0};
    if (pos < 170) {
        pos -= 85;
        return EffectColor{0, (uint8_t)(255 - pos * 3), (uint8_t)(pos * 3)};
    }
    pos -= 170;
    return EffectColor{(uint8_t)(pos * 3), 0, (uint8_t)(255 - pos * 3)};
}

EffectColor WledFxEffect::blend(const EffectColor& a, const EffectColor& b, uint8_t amount) const {
    return EffectColor{
        (uint8_t)(a.r + ((int16_t)b.r - a.r) * amount / 255),
        (uint8_t)(a.g + ((int16_t)b.g - a.g) * amount / 255),
        (uint8_t)(a.b + ((int16_t)b.b - a.b) * amount / 255),
    };
}

EffectColor WledFxEffect::builtinPalette(uint8_t paletteId, uint8_t pos) const {
    switch (paletteId) {
        case 1: return wheel(pos);  // Rainbow
        case 2: {  // Fire
            const EffectColor a{0, 0, 0};
            const EffectColor b{180, 16, 0};
            const EffectColor c{255, 120, 0};
            const EffectColor d{255, 240, 160};
            if (pos < 96) return blend(a, b, (uint8_t)(pos * 255 / 95));
            if (pos < 190) return blend(b, c, (uint8_t)((pos - 96) * 255 / 93));
            return blend(c, d, (uint8_t)((pos - 190) * 255 / 65));
        }
        case 3: {  // Ocean
            const EffectColor a{0, 8, 60};
            const EffectColor b{0, 130, 210};
            const EffectColor c{120, 255, 220};
            return (pos < 170) ? blend(a, b, (uint8_t)(pos * 255 / 169))
                               : blend(b, c, (uint8_t)((pos - 170) * 255 / 85));
        }
        case 4: {  // Forest
            const EffectColor a{0, 24, 0};
            const EffectColor b{0, 140, 36};
            const EffectColor c{220, 255, 80};
            return (pos < 180) ? blend(a, b, (uint8_t)(pos * 255 / 179))
                               : blend(b, c, (uint8_t)((pos - 180) * 255 / 75));
        }
        case 5: {  // Party
            uint8_t p = pos & 0x3F;
            uint8_t band = pos >> 6;
            const EffectColor colors[4] = {{255, 0, 80}, {0, 220, 255}, {255, 220, 0}, {110, 0, 255}};
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)(p * 4));
        }
        case 6: {  // Sunset
            const EffectColor a{60, 0, 80};
            const EffectColor b{255, 72, 0};
            const EffectColor c{255, 190, 70};
            return (pos < 150) ? blend(a, b, (uint8_t)(pos * 255 / 149))
                               : blend(b, c, (uint8_t)((pos - 150) * 255 / 105));
        }
        case 7: {  // Ice
            const EffectColor a{0, 40, 120};
            const EffectColor b{120, 230, 255};
            const EffectColor c{255, 255, 255};
            return (pos < 170) ? blend(a, b, (uint8_t)(pos * 255 / 169))
                               : blend(b, c, (uint8_t)((pos - 170) * 255 / 85));
        }
        case 8: {  // Lava
            const EffectColor a{0, 0, 0};
            const EffectColor b{160, 0, 0};
            const EffectColor c{255, 70, 0};
            const EffectColor d{255, 220, 120};
            if (pos < 88) return blend(a, b, (uint8_t)(pos * 255 / 87));
            if (pos < 180) return blend(b, c, (uint8_t)((pos - 88) * 255 / 91));
            return blend(c, d, (uint8_t)((pos - 180) * 255 / 75));
        }
        case 9: {  // Cloud
            const EffectColor a{15, 28, 70};
            const EffectColor b{90, 145, 220};
            const EffectColor c{240, 248, 255};
            return (pos < 160) ? blend(a, b, (uint8_t)(pos * 255 / 159))
                               : blend(b, c, (uint8_t)((pos - 160) * 255 / 95));
        }
        case 10: {  // Pastel
            const EffectColor colors[4] = {{255, 132, 192}, {124, 255, 190}, {132, 190, 255}, {255, 232, 120}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 11: {  // Neon
            const EffectColor colors[4] = {{255, 0, 220}, {0, 255, 255}, {130, 255, 0}, {255, 60, 0}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 12:
            return (pos < 128) ? blend(EffectColor{255, 0, 0}, EffectColor{255, 255, 255}, pos * 2)
                               : blend(EffectColor{255, 255, 255}, EffectColor{255, 0, 0}, (uint8_t)((pos - 128) * 2));
        case 13:
            return (pos < 128) ? blend(EffectColor{0, 70, 255}, EffectColor{255, 255, 255}, pos * 2)
                               : blend(EffectColor{255, 255, 255}, EffectColor{0, 70, 255}, (uint8_t)((pos - 128) * 2));
        case 14:
            return (pos < 128) ? blend(EffectColor{255, 0, 180}, EffectColor{255, 110, 0}, pos * 2)
                               : blend(EffectColor{255, 110, 0}, EffectColor{255, 0, 180}, (uint8_t)((pos - 128) * 2));
        case 15:
            return (pos < 128) ? blend(EffectColor{0, 255, 70}, EffectColor{0, 80, 255}, pos * 2)
                               : blend(EffectColor{0, 80, 255}, EffectColor{0, 255, 70}, (uint8_t)((pos - 128) * 2));
        case 16: {  // Candy
            const EffectColor colors[4] = {{255, 40, 110}, {255, 255, 255}, {80, 210, 255}, {255, 240, 110}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 17: {  // Aurora
            const EffectColor colors[4] = {{24, 16, 100}, {0, 220, 170}, {160, 80, 255}, {20, 255, 80}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 18: {  // Vintage
            const EffectColor colors[4] = {{80, 20, 10}, {220, 120, 36}, {255, 218, 150}, {20, 90, 95}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        default:
            return wheel(pos);
    }
}

EffectColor WledFxEffect::paletteAt(const EffectParams& p, uint8_t pos) const {
    if (p.paletteId > 0) return builtinPalette(p.paletteId, pos);
    uint8_t size = p.paletteSize;
    if (size > 4) size = 4;
    if (size == 0) return wheel(pos);
    if (size == 1) return p.palette[0];
    uint16_t scaled = (uint16_t)pos * size;
    uint8_t idx = (uint8_t)(scaled >> 8);
    uint8_t next = (uint8_t)((idx + 1) % size);
    return blend(p.palette[idx], p.palette[next], (uint8_t)scaled);
}

EffectColor WledFxEffect::lakeColor(const EffectParams& p, uint8_t pos) const {
    if (p.paletteSize > 1) return paletteAt(p, pos);
    const EffectColor deep{0, 16, 72};
    const EffectColor cyan{0, 170, 255};
    const EffectColor foam{120, 255, 210};
    return (pos < 160) ? blend(deep, cyan, (uint8_t)(pos * 255 / 160))
                       : blend(cyan, foam, (uint8_t)((pos - 160) * 255 / 95));
}

void WledFxEffect::renderBpm(const EffectParams& p) {
    clear();
    uint8_t inten = intensity(p);
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t pos = (uint8_t)((uint32_t)i * 255 / _numLeds + (_phase >> 2));
        uint8_t beat = wave8((uint16_t)i * 19 + (_phase << 1));
        uint8_t bri = 48 + (uint16_t)beat * (96 + inten / 2) / 255;
        setPixel(i, paletteAt(p, pos), bri);
    }
}

void WledFxEffect::renderFlow(const EffectParams& p) {
    clear();
    uint8_t inten = intensity(p);
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t wave = wave8((uint16_t)i * (8 + inten / 24) + _phase);
        uint8_t pos = (uint8_t)((uint32_t)i * 255 / _numLeds + _phase + wave / 3);
        uint8_t bri = 70 + (uint16_t)wave * 185 / 255;
        setPixel(i, paletteAt(p, pos), bri);
    }
}

void WledFxEffect::renderGravcenter(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 4);
    uint16_t center = _numLeds / 2;
    uint16_t maxRadius = (_numLeds + 1) / 2;
    uint16_t radius = (uint32_t)wave8(_phase >> 1) * maxRadius / 255;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t d = (i > center) ? (i - center) : (center - i);
        uint16_t diff = (d > radius) ? (d - radius) : (radius - d);
        if (diff > width) continue;
        uint8_t bri = (uint8_t)((uint32_t)(width - diff + 1) * 255 / (width + 1));
        addPixel(i, paletteAt(p, (uint8_t)(_phase + d * 14)), bri);
    }
}

void WledFxEffect::renderGravfreq(const EffectParams& p) {
    clear();
    uint8_t inten = intensity(p);
    uint8_t freq = 2 + inten / 32;
    uint16_t center = _numLeds / 2;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t d = (i > center) ? (i - center) : (center - i);
        uint8_t v = wave8(d * freq * 9 + _phase);
        uint8_t bri = (v < 120) ? 0 : (uint8_t)((v - 120) * 255 / 135);
        setPixel(i, paletteAt(p, (uint8_t)(_phase / 2 + d * 20)), bri);
    }
}

void WledFxEffect::renderChase(const EffectParams& p, uint8_t count) {
    clear();
    uint16_t tail = sizeParam(p, count == 2 ? 8 : 6);
    uint16_t base = (uint16_t)((_phase >> 3) % _numLeds);
    for (uint8_t h = 0; h < count; h++) {
        uint16_t head = (base + (uint32_t)h * _numLeds / count) % _numLeds;
        EffectColor c = paletteAt(p, (uint8_t)(h * 255 / count + _phase));
        for (uint16_t i = 0; i < _numLeds; i++) {
            uint16_t dist = (head + _numLeds - i) % _numLeds;
            if (dist > tail) continue;
            uint8_t bri = (uint8_t)((uint32_t)(tail - dist + 1) * 255 / (tail + 1));
            addPixel(i, c, bri);
        }
    }
}

void WledFxEffect::renderChunchun(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 4);
    uint16_t spacing = width * 3 + 2;
    uint16_t base = (uint16_t)((_phase >> 3) % (_numLeds ? _numLeds : 1));
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t center = (base + (uint32_t)i * spacing) % _numLeds;
        drawBlob(center, width, paletteAt(p, (uint8_t)(i * 64 + _phase)), 235);
    }
}

void WledFxEffect::renderLake(const EffectParams& p) {
    clear();
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t a = wave8(i * 8 + (_phase >> 1));
        uint8_t b = wave8(i * 17 - (_phase >> 2));
        uint8_t v = (uint8_t)(((uint16_t)a + b) / 2);
        setPixel(i, lakeColor(p, v), (uint8_t)(50 + (uint16_t)v * 205 / 255));
    }
}

void WledFxEffect::renderMeteor(const EffectParams& p) {
    uint8_t keep = 165 + intensity(p) / 4;
    fade(keep);
    uint16_t tail = sizeParam(p, 10);
    uint16_t head = (uint16_t)((_phase >> 4) % _numLeds);
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 1));
    for (uint16_t d = 0; d <= tail; d++) {
        uint16_t idx = (head + _numLeds - d) % _numLeds;
        uint8_t bri = (uint8_t)((uint32_t)(tail - d + 1) * 255 / (tail + 1));
        addPixel(idx, c, bri);
    }
}

void WledFxEffect::renderNoise3(const EffectParams& p) {
    clear();
    uint8_t scale = 10 + intensity(p) / 7;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t n = noise8(i * scale, (uint16_t)(_phase >> 1));
        uint8_t n2 = noise8(i * (scale / 2 + 5) + 71, (uint16_t)(_phase >> 2));
        uint8_t v = (uint8_t)(((uint16_t)n + n2) / 2);
        setPixel(i, paletteAt(p, v), v);
    }
}

void WledFxEffect::renderOscillate(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 5);
    uint16_t span = _numLeds > 1 ? _numLeds - 1 : 1;
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t w = wave8((_phase >> (i == 0 ? 1 : 2)) + i * 85);
        uint16_t center = (uint32_t)w * span / 255;
        drawBlob(center, width, paletteAt(p, (uint8_t)(i * 85 + _phase)), 240);
    }
}

void WledFxEffect::renderRipple(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 3);
    uint16_t maxRadius = _numLeds / 2 + width + 1;
    if (_rippleRadius == 0 || _rippleRadius > maxRadius) {
        _rippleOrigin = randomLed();
        _rippleRadius = 1;
    }
    EffectColor c = paletteAt(p, (uint8_t)(_frame * 17));
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t d = circularDistance(i, _rippleOrigin);
        uint16_t diff = (d > _rippleRadius) ? (d - _rippleRadius) : (_rippleRadius - d);
        if (diff > width) continue;
        uint8_t bri = (uint8_t)((uint32_t)(width - diff + 1) * 255 / (width + 1));
        addPixel(i, c, bri);
    }
    _rippleRadius += 1 + (p.speed > 500 ? 1 : 0);
}

void WledFxEffect::renderRunning(const EffectParams& p) {
    clear();
    uint8_t inten = intensity(p);
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t v = wave8(i * (8 + inten / 28) + _phase);
        uint8_t bri = (v < 80) ? 0 : (uint8_t)((v - 80) * 255 / 175);
        setPixel(i, paletteAt(p, (uint8_t)(_phase + i * 8)), bri);
    }
}

void WledFxEffect::renderStrobe(const EffectParams& p) {
    uint16_t cycle = 24 + (uint16_t)(1010 - (p.speed < 10 ? 10 : (p.speed > 1000 ? 1000 : p.speed))) / 18;
    uint16_t onTime = 2 + (uint16_t)intensity(p) * (cycle / 2 + 1) / 255;
    bool on = (_frame % cycle) < onTime;
    if (!on) {
        clear();
        return;
    }
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 1));
    for (uint16_t i = 0; i < _numLeds; i++) setPixel(i, c, 255);
}

void WledFxEffect::renderFade(const EffectParams& p) {
    uint8_t pos = (uint8_t)(_phase >> 1);
    EffectColor c = paletteAt(p, pos);
    uint8_t wave = wave8(_phase);
    uint8_t minBri = 20 + intensity(p) / 6;
    uint8_t bri = minBri + (uint16_t)wave * (255 - minBri) / 255;
    for (uint16_t i = 0; i < _numLeds; i++) setPixel(i, c, bri);
}

void WledFxEffect::renderRainbow(const EffectParams& p) {
    uint8_t spread = 4 + intensity(p) / 8;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t pos = (uint8_t)((uint32_t)i * spread + (_phase >> 1));
        setPixel(i, builtinPalette(1, pos), 255);
    }
}
