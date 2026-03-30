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

    // Blokující — čeká na ukončení tasku, pak zhasne LED.
    void stop();

    bool isRunning() const { return _taskHandle != nullptr; }

    // Aktualizuje parametry za běhu (rychlost, barva, velikost tečky).
    void setParams(const EffectParams& p);

private:
    static void taskEntry(void* arg);
    void        runTask();

    ILedDriver&  _leds;
    EffectParams _params  = {};
    IEffect*     _effect  = nullptr;

    TaskHandle_t  _taskHandle  = nullptr;
    volatile bool _taskRunning = false;
};
