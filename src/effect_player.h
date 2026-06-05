#pragma once
#include <Arduino.h>
#include "led_driver.h"
#include "effect.h"

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
    uint16_t fpsX10() const { return _currentFpsX10; }

    // Aktualizuje parametry za běhu (rychlost, barva, velikost tečky).
    void setParams(const EffectParams& p);

private:
    static void taskEntry(void* arg);
    void        runTask();

    ILedDriver&  _leds;
    EffectParams _params  = {};
    IEffect*     _effect  = nullptr;
    portMUX_TYPE _paramsMux = portMUX_INITIALIZER_UNLOCKED;

    TaskHandle_t  _taskHandle  = nullptr;
    volatile bool _taskRunning = false;
    uint32_t      _fpsWindowFrames = 0;
    int64_t       _fpsWindowStartUs = 0;
    uint16_t      _currentFpsX10 = 0;
};
