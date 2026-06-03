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
        case EFFECT_TWINKLE:    renderTwinkle(p); break;
        case EFFECT_SPARKLE:    renderSparkle(p); break;
        case EFFECT_FIREWORKS:  renderFireworks(p); break;
        case EFFECT_SCANNER:    renderScanner(p, false); break;
        case EFFECT_SCANNER_DUAL: renderScanner(p, true); break;
        case EFFECT_THEATER:    renderTheater(p); break;
        case EFFECT_COLOR_WIPE: renderColorWipe(p); break;
        case EFFECT_JUGGLE:     renderJuggle(p); break;
        case EFFECT_SINELON:    renderSinelon(p); break;
        case EFFECT_FIRE:       renderFire(p); break;
        case EFFECT_PLASMA:     renderPlasma(p); break;
        case EFFECT_GRADIENT:   renderGradient(p); break;
        case EFFECT_BREATH:     renderBreath(p); break;
        case EFFECT_DOTS:       renderDots(p); break;
        case EFFECT_COUNTER_CHASE: renderCounterChase(p); break;
        case EFFECT_SPLIT_CHASE:   renderSplitChase(p); break;
        case EFFECT_COLLIDE:       renderCollide(p); break;
        case EFFECT_SAW:           renderSaw(p); break;
        case EFFECT_CHEVRON:       renderChevron(p); break;
        case EFFECT_PULSE_TRAIN:   renderPulseTrain(p); break;
        case EFFECT_CROSS_WAVES:   renderCrossWaves(p); break;
        case EFFECT_BARBER_POLE:   renderBarberPole(p); break;
        case EFFECT_SCAN_BARS:     renderScanBars(p); break;
        case EFFECT_PRISM:         renderPrism(p); break;
        case EFFECT_SPIN:          renderSpin(p); break;
        case EFFECT_TWIST:         renderTwist(p); break;
        case EFFECT_CHASE:         renderChaseSingle(p); break;
        case EFFECT_FIRE_CLASSIC:  renderFireClassic(p); break;
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
        case 9: {  // Pastel
            const EffectColor colors[4] = {{255, 132, 192}, {124, 255, 190}, {132, 190, 255}, {255, 232, 120}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 10: {  // Neon
            const EffectColor colors[4] = {{255, 0, 220}, {0, 255, 255}, {130, 255, 0}, {255, 60, 0}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 11: {  // Candy
            const EffectColor colors[4] = {{255, 40, 110}, {255, 255, 255}, {80, 210, 255}, {255, 240, 110}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 12: {  // Aurora
            const EffectColor colors[4] = {{24, 16, 100}, {0, 220, 170}, {160, 80, 255}, {20, 255, 80}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 13: {  // Vintage
            const EffectColor colors[4] = {{80, 20, 10}, {220, 120, 36}, {255, 218, 150}, {20, 90, 95}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 14: {  // Rainbow stripe
            uint8_t striped = (pos & 0x20) ? pos : (uint8_t)(pos + 40);
            return wheel(striped);
        }
        case 15: {  // Blue purple
            const EffectColor colors[4] = {{0, 12, 80}, {0, 120, 255}, {150, 0, 255}, {255, 40, 210}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 16: {  // Pink candy
            const EffectColor colors[4] = {{255, 0, 92}, {255, 180, 220}, {255, 255, 255}, {120, 220, 255}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 17: {  // C9
            const EffectColor colors[4] = {{255, 0, 0}, {255, 160, 0}, {0, 180, 70}, {0, 80, 255}};
            return colors[(pos >> 6) & 3];
        }
        case 18: {  // Tiamat
            const EffectColor colors[4] = {{18, 0, 70}, {0, 180, 190}, {255, 40, 120}, {255, 180, 40}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 19: {  // Dry wet
            const EffectColor colors[4] = {{255, 160, 70}, {255, 230, 150}, {40, 180, 255}, {0, 30, 120}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 20:
            return (pos < 128) ? blend(EffectColor{255, 0, 0}, EffectColor{0, 70, 255}, pos * 2)
                               : blend(EffectColor{0, 70, 255}, EffectColor{255, 0, 0}, (uint8_t)((pos - 128) * 2));
        case 21:
            return (pos < 128) ? blend(EffectColor{255, 220, 0}, EffectColor{0, 255, 70}, pos * 2)
                               : blend(EffectColor{0, 255, 70}, EffectColor{255, 220, 0}, (uint8_t)((pos - 128) * 2));
        case 22:
            return (pos < 128) ? blend(EffectColor{120, 0, 255}, EffectColor{0, 255, 100}, pos * 2)
                               : blend(EffectColor{0, 255, 100}, EffectColor{120, 0, 255}, (uint8_t)((pos - 128) * 2));
        case 23: {  // Warm white
            const EffectColor a{255, 120, 40};
            const EffectColor b{255, 235, 180};
            return (pos < 128) ? blend(a, b, pos * 2) : blend(b, a, (uint8_t)((pos - 128) * 2));
        }
        case 24: {  // Aqua magenta
            const EffectColor colors[4] = {{0, 255, 210}, {0, 80, 255}, {255, 0, 220}, {255, 255, 255}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 25:
            return (pos < 128) ? blend(EffectColor{255, 0, 0}, EffectColor{255, 255, 255}, pos * 2)
                               : blend(EffectColor{255, 255, 255}, EffectColor{0, 80, 255}, (uint8_t)((pos - 128) * 2));
        case 26: {  // Matrix
            const EffectColor colors[4] = {{0, 20, 0}, {0, 255, 70}, {186, 255, 128}, {0, 80, 20}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 27: {  // Sakura
            const EffectColor colors[4] = {{255, 45, 133}, {255, 210, 232}, {255, 255, 255}, {180, 20, 90}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 28: {  // Electric
            const EffectColor colors[4] = {{0, 20, 255}, {0, 240, 255}, {255, 255, 255}, {0, 90, 180}};
            uint8_t band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (uint8_t)((pos & 0x3F) * 4));
        }
        case 29: {  // Amber teal
            const EffectColor colors[4] = {{255, 138, 0}, {255, 224, 110}, {0, 180, 170}, {0, 55, 80}};
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

void WledFxEffect::renderTwinkle(const EffectParams& p) {
    clear();
    uint8_t density = 20 + intensity(p) / 2;
    uint16_t tick = _phase >> 5;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t h = hash8(i * 17, tick);
        if (h > density) continue;
        uint8_t age = hash8(i * 23, tick + 19);
        uint8_t bri = age < 128 ? (uint8_t)(age * 2) : (uint8_t)((255 - age) * 2);
        setPixel(i, paletteAt(p, (uint8_t)(h + _phase)), bri);
    }
}

void WledFxEffect::renderSparkle(const EffectParams& p) {
    fade(150 + intensity(p) / 4);
    uint8_t count = 1 + intensity(p) / 32;
    for (uint8_t n = 0; n < count; n++) {
        uint16_t i = randomLed();
        setPixel(i, paletteAt(p, (uint8_t)(_phase + n * 37)), 255);
    }
}

void WledFxEffect::renderFireworks(const EffectParams& p) {
    fade(170);
    uint8_t bursts = 2 + intensity(p) / 70;
    uint16_t span = _numLeds ? _numLeds : 1;
    uint16_t width = sizeParam(p, 4);
    for (uint8_t b = 0; b < bursts; b++) {
        uint16_t local = (_phase >> 2) + b * 73;
        uint16_t origin = (uint16_t)((uint32_t)hash8(b * 41, local >> 6) * span / 255);
        uint8_t radius = wave8(local);
        uint16_t centerA = (origin + (uint32_t)radius * span / 510) % span;
        uint16_t centerB = (origin + span - (uint32_t)radius * span / 510) % span;
        EffectColor c = paletteAt(p, (uint8_t)(b * 70 + _phase));
        drawBlob(centerA, width, c, 210);
        drawBlob(centerB, width, c, 210);
    }
}

void WledFxEffect::renderScanner(const EffectParams& p, bool dual) {
    fade(110 + intensity(p) / 3);
    uint16_t width = sizeParam(p, 4);
    uint16_t span = _numLeds > 1 ? _numLeds - 1 : 1;
    uint16_t center = (uint32_t)wave8(_phase >> 1) * span / 255;
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 1));
    drawBlob(center, width, c, 255);
    if (dual) drawBlob(span - center, width, paletteAt(p, (uint8_t)(_phase + 128)), 255);
}

void WledFxEffect::renderTheater(const EffectParams& p) {
    clear();
    uint8_t spacing = 3 + intensity(p) / 42;
    uint8_t duty = 1 + (sizeParam(p, 2) % spacing);
    uint16_t offset = (_phase >> 4) % spacing;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t slot = (i + offset) % spacing;
        if (slot < duty) setPixel(i, paletteAt(p, (uint8_t)(i * 12 + _phase)), 255);
    }
}

void WledFxEffect::renderColorWipe(const EffectParams& p) {
    clear();
    uint16_t span = _numLeds ? _numLeds : 1;
    uint16_t head = (_phase >> 4) % (span + 1);
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 2));
    for (uint16_t i = 0; i < head && i < _numLeds; i++) setPixel(i, c, 255);
}

void WledFxEffect::renderJuggle(const EffectParams& p) {
    fade(120 + intensity(p) / 3);
    uint8_t dots = 3 + intensity(p) / 43;
    uint16_t span = _numLeds > 1 ? _numLeds - 1 : 1;
    for (uint8_t d = 0; d < dots; d++) {
        uint16_t pos = (uint32_t)wave8((_phase >> 1) + d * 37) * span / 255;
        addPixel(pos, paletteAt(p, (uint8_t)(d * 255 / dots + _phase)), 230);
    }
}

void WledFxEffect::renderSinelon(const EffectParams& p) {
    fade(135 + intensity(p) / 4);
    uint16_t width = sizeParam(p, 3);
    uint16_t span = _numLeds > 1 ? _numLeds - 1 : 1;
    uint16_t pos = (uint32_t)wave8(_phase >> 1) * span / 255;
    drawBlob(pos, width, paletteAt(p, (uint8_t)(_phase >> 1)), 255);
}

void WledFxEffect::renderFire(const EffectParams& p) {
    uint8_t cooling = 10 + (255 - intensity(p)) / 5;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t heat = noise8(i * 18, (uint16_t)(_phase >> 1));
        uint16_t shaped = heat;
        shaped = (shaped * shaped) >> 8;
        if (i > _numLeds / 2) shaped = shaped > cooling ? shaped - cooling : 0;
        setPixel(i, builtinPalette(p.paletteId ? p.paletteId : 2, (uint8_t)shaped), shaped);
    }
}

void WledFxEffect::renderPlasma(const EffectParams& p) {
    clear();
    uint8_t spread = 8 + intensity(p) / 10;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t a = wave8(i * spread + _phase);
        uint8_t b = wave8(i * (spread / 2 + 7) - (_phase >> 1));
        uint8_t c = wave8((uint16_t)i * 3 + (_phase >> 2));
        uint8_t v = (uint8_t)(((uint16_t)a + b + c) / 3);
        setPixel(i, paletteAt(p, v), 255);
    }
}

void WledFxEffect::renderGradient(const EffectParams& p) {
    uint8_t spread = 2 + intensity(p) / 10;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t pos = (uint8_t)((uint32_t)i * spread + (_phase >> 2));
        setPixel(i, paletteAt(p, pos), 255);
    }
}

void WledFxEffect::renderBreath(const EffectParams& p) {
    uint8_t wave = wave8(_phase >> 1);
    uint8_t floor = intensity(p) / 8;
    uint8_t bri = floor + (uint16_t)wave * (255 - floor) / 255;
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 3));
    for (uint16_t i = 0; i < _numLeds; i++) setPixel(i, c, bri);
}

void WledFxEffect::renderDots(const EffectParams& p) {
    clear();
    uint8_t count = 2 + intensity(p) / 28;
    uint16_t width = sizeParam(p, 2);
    uint16_t span = _numLeds ? _numLeds : 1;
    for (uint8_t d = 0; d < count; d++) {
        uint16_t pos = ((uint32_t)(_phase >> 3) * (d + 1) + (uint32_t)d * span / count) % span;
        drawBlob(pos, width, paletteAt(p, (uint8_t)(d * 255 / count + _phase)), 230);
    }
}

void WledFxEffect::renderCounterChase(const EffectParams& p) {
    clear();
    uint16_t tail = sizeParam(p, 8);
    uint16_t span = _numLeds ? _numLeds : 1;
    uint16_t a = (_phase >> 3) % span;
    uint16_t b = (span - 1) - a;
    for (uint16_t d = 0; d <= tail; d++) {
        uint8_t bri = (uint8_t)((uint32_t)(tail - d + 1) * 255 / (tail + 1));
        addPixel((a + span - d) % span, paletteAt(p, (uint8_t)(_phase)), bri);
        addPixel((b + d) % span, paletteAt(p, (uint8_t)(_phase + 128)), bri);
    }
}

void WledFxEffect::renderSplitChase(const EffectParams& p) {
    clear();
    uint16_t tail = sizeParam(p, 6);
    uint16_t half = _numLeds / 2;
    uint16_t span = half ? half : 1;
    uint16_t p0 = (_phase >> 3) % span;
    for (uint16_t d = 0; d <= tail; d++) {
        uint8_t bri = (uint8_t)((uint32_t)(tail - d + 1) * 255 / (tail + 1));
        addPixel((p0 + span - d) % span, paletteAt(p, (uint8_t)(_phase)), bri);
        addPixel(half + ((span - 1 - p0 + d) % span), paletteAt(p, (uint8_t)(_phase + 96)), bri);
    }
}

void WledFxEffect::renderCollide(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 4);
    uint16_t span = _numLeds > 1 ? _numLeds - 1 : 1;
    uint16_t pos = (uint32_t)wave8(_phase >> 1) * span / 255;
    drawBlob(pos, width, paletteAt(p, (uint8_t)(_phase)), 255);
    drawBlob(span - pos, width, paletteAt(p, (uint8_t)(_phase + 128)), 255);
}

void WledFxEffect::renderSaw(const EffectParams& p) {
    clear();
    uint8_t width = 12 + intensity(p) / 5;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t v = (uint8_t)((i * width + _phase) & 255);
        setPixel(i, paletteAt(p, v), v);
    }
}

void WledFxEffect::renderChevron(const EffectParams& p) {
    clear();
    uint16_t center = _numLeds / 2;
    uint8_t width = 12 + intensity(p) / 6;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t d = (i > center) ? (i - center) : (center - i);
        uint8_t v = wave8(d * width + _phase);
        setPixel(i, paletteAt(p, (uint8_t)(d * 12 + _phase)), v);
    }
}

void WledFxEffect::renderPulseTrain(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 3);
    uint8_t count = 2 + intensity(p) / 40;
    uint16_t span = _numLeds ? _numLeds : 1;
    for (uint8_t k = 0; k < count; k++) {
        uint16_t pos = ((uint32_t)(_phase >> 3) + (uint32_t)k * span / count) % span;
        drawBlob(pos, width, paletteAt(p, (uint8_t)(k * 255 / count + _phase)), 255);
    }
}

void WledFxEffect::renderCrossWaves(const EffectParams& p) {
    clear();
    uint8_t freq = 8 + intensity(p) / 18;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t a = wave8(i * freq + _phase);
        uint8_t b = wave8(i * freq - _phase);
        uint8_t v = (uint8_t)(((uint16_t)a + b) / 2);
        setPixel(i, paletteAt(p, (uint8_t)(i * 9 + _phase)), v);
    }
}

void WledFxEffect::renderBarberPole(const EffectParams& p) {
    clear();
    uint8_t bands = 18 + intensity(p) / 8;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t v = (uint8_t)(i * bands + (_phase >> 1));
        uint8_t bri = ((v & 0x7F) < 64) ? 255 : 70;
        setPixel(i, paletteAt(p, v), bri);
    }
}

void WledFxEffect::renderScanBars(const EffectParams& p) {
    fade(90 + intensity(p) / 2);
    uint16_t width = sizeParam(p, 2);
    uint8_t bars = 2 + intensity(p) / 64;
    uint16_t span = _numLeds ? _numLeds : 1;
    for (uint8_t b = 0; b < bars; b++) {
        uint16_t pos = ((uint32_t)(_phase >> 2) * (b + 1) + (uint32_t)b * span / bars) % span;
        drawBlob(pos, width, paletteAt(p, (uint8_t)(_phase + b * 70)), 255);
    }
}

void WledFxEffect::renderPrism(const EffectParams& p) {
    clear();
    uint8_t spread = 6 + intensity(p) / 14;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t pos = (uint8_t)(i * spread + (_phase >> 1));
        uint8_t gate = wave8(i * 11 - (_phase >> 2));
        setPixel(i, paletteAt(p, pos), (uint8_t)(70 + (uint16_t)gate * 185 / 255));
    }
}

void WledFxEffect::renderSpin(const EffectParams& p) {
    clear();
    uint16_t width = sizeParam(p, 5);
    uint16_t span = _numLeds ? _numLeds : 1;
    uint16_t pos = (_phase >> 2) % span;
    drawBlob(pos, width, paletteAt(p, (uint8_t)(_phase)), 255);
    drawBlob((pos + span / 3) % span, width, paletteAt(p, (uint8_t)(_phase + 85)), 220);
    drawBlob((pos + (2 * span) / 3) % span, width, paletteAt(p, (uint8_t)(_phase + 170)), 220);
}

void WledFxEffect::renderTwist(const EffectParams& p) {
    clear();
    uint8_t density = 10 + intensity(p) / 9;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint8_t a = wave8(i * density + _phase);
        uint8_t b = wave8(i * (density + 9) - (_phase >> 1));
        uint8_t v = (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
        setPixel(i, paletteAt(p, (uint8_t)(_phase + i * 13)), v);
    }
}

void WledFxEffect::renderChaseSingle(const EffectParams& p) {
    clear();
    uint16_t tail = sizeParam(p, 10);
    uint16_t span = _numLeds ? _numLeds : 1;
    uint16_t head = (_phase >> 3) % span;
    EffectColor c = paletteAt(p, (uint8_t)(_phase >> 1));
    for (uint16_t d = 0; d <= tail; d++) {
        uint16_t idx = (head + span - d) % span;
        uint8_t bri = (uint8_t)((uint32_t)(tail - d + 1) * 255 / (tail + 1));
        addPixel(idx, c, bri);
    }
    uint8_t sparks = intensity(p) / 64;
    for (uint8_t s = 0; s < sparks; s++) {
        uint16_t idx = (head + span / 2 + s * 7) % span;
        setPixel(idx, paletteAt(p, (uint8_t)(_phase + 96 + s * 53)), 120);
    }
}

void WledFxEffect::renderFireClassic(const EffectParams& p) {
    uint8_t scale = 12 + intensity(p) / 8;
    uint8_t paletteId = p.paletteId ? p.paletteId : 2;
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t fromBase = _numLeds - 1 - i;
        uint8_t rise = (uint8_t)((uint32_t)fromBase * 255 / (_numLeds ? _numLeds : 1));
        uint8_t n1 = noise8(i * scale + (_phase >> 1), (uint16_t)(_phase >> 2));
        uint8_t n2 = wave8(i * (scale / 2 + 9) - _phase);
        uint16_t heat = (uint16_t)n1 * 2 / 3 + (uint16_t)n2 / 3;
        heat = (heat * (255 - rise / 2)) / 255;
        if (fromBase < _numLeds / 5) heat = heat + (255 - heat) / 3;
        uint8_t v = heat > 255 ? 255 : (uint8_t)heat;
        setPixel(i, builtinPalette(paletteId, v), v);
    }
}
