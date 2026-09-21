#pragma once

#include <Arduino.h>

#include "effect.h"
#include "audio_group.h"

struct AudioReactiveFrame {
    uint8_t volume = 0;
    uint8_t bass = 0;
    uint8_t mid = 0;
    uint8_t treble = 0;
    uint8_t beat = 0;
    uint8_t spectrum[64] = {};
    bool detailed = false;
    bool group = false;
    uint32_t groupAgeMs = 0, groupLocalMs = 0, groupEpoch = 0;
    uint32_t beatAt[4] = {AudioGroup::NoBeat, AudioGroup::NoBeat, AudioGroup::NoBeat, AudioGroup::NoBeat};
    uint32_t sequence = 0;
    uint32_t beatSequence = 0;
};

struct AudioReactiveSettings {
    uint8_t mode = 0;
    uint8_t speed = 50;
    uint8_t width = 35;
    uint8_t decay = 50;
    uint8_t brightness = 80;
    uint8_t response = 0; // Full spectrum, bass, mids, treble.
    bool mirror = false;
    EffectColor colors[3] = {{255, 96, 0}, {0, 220, 210}, {255, 32, 112}};
};

class AudioReactiveInput {
public:
    void update(uint8_t volume, uint8_t bass, uint8_t mid, uint8_t treble, uint8_t beat);
    void updateSpectrum(const uint8_t* levels, const uint8_t* spectrum);
    void updateGroup(const AudioGroup::Packet& packet, uint32_t ageMs);
    void clear();
    AudioReactiveFrame snapshot();
    void setSettings(const AudioReactiveSettings& settings);
    AudioReactiveSettings settingsSnapshot();

private:
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    AudioReactiveFrame _frame = {};
    AudioReactiveSettings _settings;
};

class AudioReactiveEffect : public IEffect {
public:
    explicit AudioReactiveEffect(AudioReactiveInput& input) : _input(input) {}
    ~AudioReactiveEffect();

    void reset(const EffectParams& p, uint16_t numLeds) override;
    void update(ILedDriver& leds, const EffectParams& p) override;
    uint32_t intervalUs(const EffectParams& p) const override;
    bool ready() const { return _buf != nullptr && _numLeds > 0; }

private:
    static uint8_t triangle8(uint16_t phase);
    static uint8_t scale8(uint8_t value, uint8_t scale);
    static uint8_t addSat(uint8_t a, uint8_t b);
    static EffectColor gradient(const AudioReactiveSettings& settings, uint8_t position);

    AudioReactiveInput& _input;
    uint8_t* _buf = nullptr;
    uint16_t _numLeds = 0;
    uint16_t _phase = 0;
    uint32_t _lastPacketMs = 0;
    uint32_t _lastUpdateMs = 0;
    float _flowAgeMs = 0;
    bool _wasGroup = false;
    uint32_t _groupEpoch = 0;
    uint32_t _flowStep = AudioGroup::NoBeat;
    uint8_t _flowHead = 0;
    uint8_t _flow[64][3] = {};
    uint32_t _lastBeatSequence = 0;
    uint16_t _ripple[4] = {65535, 65535, 65535, 65535};
    uint8_t _rippleIndex = 0;
    uint8_t _step = 0;
    float _peak = 0;
    uint8_t _mode = 255;
    uint8_t _volume = 0;
    uint8_t _bass = 0;
    uint8_t _mid = 0;
    uint8_t _treble = 0;
    uint8_t _beatPulse = 0;
    uint8_t _spectrum[64] = {};
    uint8_t _target[68] = {};
    float _visual[68] = {};
    bool _hasAudio = false;
    uint8_t _onset[64] = {};
    uint8_t _envelope[64] = {};
    uint8_t _response = 255;
    uint8_t _selectedPrevious = 0;
    uint32_t _selectedBeatMs = 0;
    uint32_t _lastSequence = 0;
};
