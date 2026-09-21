#pragma once
#include <Arduino.h>
#include "led_driver.h"
#include "effect.h"
#include "audio_reactive_effect.h"
#include <atomic>

class EffectPlayer {
public:
    explicit EffectPlayer(ILedDriver& leds);
    ~EffectPlayer();

    // Zastaví aktuální efekt (pokud běží), spustí nový na Core 1 s prioritou 4.
    void start(const EffectParams& p);
    void apply(const EffectParams& p);

    // Blokující — čeká na ukončení tasku, pak zhasne LED.
    void stop();

    bool isRunning() const { return _taskHandle != nullptr; }
    bool isAudioReactive();
    uint32_t controlRevision() const { return _controlRevision.load(); }
    uint16_t fpsX10() const { return _currentFpsX10; }

    // Aktualizuje parametry za běhu (rychlost, barva, velikost tečky).
    void setParams(const EffectParams& p);
    void updateAudioReactive(uint8_t volume, uint8_t bass, uint8_t mid, uint8_t treble,
                             uint8_t beat);
    void clearAudioReactive();
    void updateAudioSpectrum(const uint8_t* levels, const uint8_t* spectrum) {
        _audioInput.updateSpectrum(levels, spectrum);
    }
    void updateAudioGroup(const AudioGroup::Packet& packet, uint32_t ageMs) { _audioInput.updateGroup(packet, ageMs); }
    void setAudioSettings(const AudioReactiveSettings& settings) { _audioInput.setSettings(settings); }

private:
    static void taskEntry(void* arg);
    void        runTask();

    ILedDriver&  _leds;
    EffectParams _params  = {};
    IEffect*     _effect  = nullptr;
    AudioReactiveInput _audioInput;
    std::atomic<uint32_t> _controlRevision{0};
    portMUX_TYPE _paramsMux = portMUX_INITIALIZER_UNLOCKED;

    TaskHandle_t  _taskHandle  = nullptr;
    volatile bool _taskRunning = false;
    uint32_t      _fpsWindowFrames = 0;
    int64_t       _fpsWindowStartUs = 0;
    uint16_t      _currentFpsX10 = 0;
};
