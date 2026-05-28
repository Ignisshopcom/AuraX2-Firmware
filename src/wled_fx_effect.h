#pragma once
#include "effect.h"

enum AuraXEffectId : uint8_t {
    EFFECT_SOLID      = 1,
    EFFECT_ANDROID    = 2,
    EFFECT_BPM        = 10,
    EFFECT_FLOW       = 11,
    EFFECT_GRAVCENTER = 12,
    EFFECT_GRAVFREQ   = 13,
    EFFECT_CHASE2     = 14,
    EFFECT_CHASE3     = 15,
    EFFECT_CHUNCHUN   = 16,
    EFFECT_LAKE       = 17,
    EFFECT_METEOR     = 18,
    EFFECT_NOISE3     = 19,
    EFFECT_OSCILLATE  = 20,
    EFFECT_RIPPLE     = 21,
    EFFECT_RUNNING    = 22,
    EFFECT_STROBE     = 23,
    EFFECT_FADE       = 24,
    EFFECT_RAINBOW    = 25,
};

class WledFxEffect : public IEffect {
public:
    ~WledFxEffect();
    void     reset(const EffectParams& p, uint16_t numLeds) override;
    void     update(ILedDriver& leds, const EffectParams& p) override;
    uint32_t intervalUs(const EffectParams& p) const override;

private:
    uint8_t* _buf = nullptr;
    uint16_t _numLeds = 0;
    uint32_t _phase = 0;
    uint32_t _frame = 0;
    uint32_t _rng = 0x1234abcd;
    uint16_t _rippleOrigin = 0;
    uint16_t _rippleRadius = 0;

    void clear();
    void fade(uint8_t keep);
    void setPixel(uint16_t i, const EffectColor& c, uint8_t scale = 255);
    void addPixel(uint16_t i, const EffectColor& c, uint8_t scale = 255);
    void drawBlob(uint16_t center, uint16_t width, const EffectColor& c, uint8_t scale = 255);
    void show(ILedDriver& leds);

    uint16_t speedStep(const EffectParams& p) const;
    uint8_t  intensity(const EffectParams& p) const;
    uint16_t sizeParam(const EffectParams& p, uint16_t fallback) const;
    uint16_t circularDistance(uint16_t a, uint16_t b) const;
    uint8_t  wave8(uint16_t x) const;
    uint8_t  hash8(uint16_t x, uint16_t y) const;
    uint8_t  noise8(uint16_t x, uint16_t t) const;
    uint16_t randomLed();
    EffectColor wheel(uint8_t pos) const;
    EffectColor blend(const EffectColor& a, const EffectColor& b, uint8_t amount) const;
    EffectColor builtinPalette(uint8_t paletteId, uint8_t pos) const;
    EffectColor paletteAt(const EffectParams& p, uint8_t pos) const;
    EffectColor lakeColor(const EffectParams& p, uint8_t pos) const;

    void renderBpm(const EffectParams& p);
    void renderFlow(const EffectParams& p);
    void renderGravcenter(const EffectParams& p);
    void renderGravfreq(const EffectParams& p);
    void renderChase(const EffectParams& p, uint8_t count);
    void renderChunchun(const EffectParams& p);
    void renderLake(const EffectParams& p);
    void renderMeteor(const EffectParams& p);
    void renderNoise3(const EffectParams& p);
    void renderOscillate(const EffectParams& p);
    void renderRipple(const EffectParams& p);
    void renderRunning(const EffectParams& p);
    void renderStrobe(const EffectParams& p);
    void renderFade(const EffectParams& p);
    void renderRainbow(const EffectParams& p);
};
