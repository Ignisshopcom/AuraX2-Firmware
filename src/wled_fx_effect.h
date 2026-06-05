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
    EFFECT_TWINKLE    = 26,
    EFFECT_SPARKLE    = 27,
    EFFECT_FIREWORKS  = 28,
    EFFECT_SCANNER    = 29,
    EFFECT_SCANNER_DUAL = 30,
    EFFECT_THEATER    = 31,
    EFFECT_COLOR_WIPE = 32,
    EFFECT_JUGGLE     = 33,
    EFFECT_SINELON    = 34,
    EFFECT_FIRE       = 35,
    EFFECT_PLASMA     = 36,
    EFFECT_GRADIENT   = 37,
    EFFECT_BREATH     = 38,
    EFFECT_DOTS       = 39,
    EFFECT_COUNTER_CHASE = 40,
    EFFECT_SPLIT_CHASE   = 41,
    EFFECT_COLLIDE       = 42,
    EFFECT_SAW           = 43,
    EFFECT_CHEVRON       = 44,
    EFFECT_PULSE_TRAIN   = 45,
    EFFECT_CROSS_WAVES   = 46,
    EFFECT_BARBER_POLE   = 47,
    EFFECT_SCAN_BARS     = 48,
    EFFECT_PRISM         = 49,
    EFFECT_SPIN          = 50,
    EFFECT_TWIST         = 51,
    EFFECT_CHASE         = 52,
    EFFECT_FIRE_CLASSIC  = 53,
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
    void drawSoftBlob(uint32_t centerQ8, uint16_t width, const EffectColor& c, uint8_t scale = 255);
    void drawSoftTrail(uint32_t headQ8, uint16_t tail, const EffectColor& c, uint8_t scale = 255, bool reverse = false);
    void show(ILedDriver& leds);

    uint16_t scaledSpeed(const EffectParams& p) const;
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
    void renderTwinkle(const EffectParams& p);
    void renderSparkle(const EffectParams& p);
    void renderFireworks(const EffectParams& p);
    void renderScanner(const EffectParams& p, bool dual);
    void renderTheater(const EffectParams& p);
    void renderColorWipe(const EffectParams& p);
    void renderJuggle(const EffectParams& p);
    void renderSinelon(const EffectParams& p);
    void renderFire(const EffectParams& p);
    void renderPlasma(const EffectParams& p);
    void renderGradient(const EffectParams& p);
    void renderBreath(const EffectParams& p);
    void renderDots(const EffectParams& p);
    void renderCounterChase(const EffectParams& p);
    void renderSplitChase(const EffectParams& p);
    void renderCollide(const EffectParams& p);
    void renderSaw(const EffectParams& p);
    void renderChevron(const EffectParams& p);
    void renderPulseTrain(const EffectParams& p);
    void renderCrossWaves(const EffectParams& p);
    void renderBarberPole(const EffectParams& p);
    void renderScanBars(const EffectParams& p);
    void renderPrism(const EffectParams& p);
    void renderSpin(const EffectParams& p);
    void renderTwist(const EffectParams& p);
    void renderChaseSingle(const EffectParams& p);
    void renderFireClassic(const EffectParams& p);
};
