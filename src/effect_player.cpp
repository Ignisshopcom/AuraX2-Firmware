#include "effect_player.h"
#include "solid_effect.h"
#include "android_effect.h"
#include <esp_timer.h>

EffectPlayer::EffectPlayer(ILedDriver& leds) : _leds(leds) {}

EffectPlayer::~EffectPlayer() {
    stop();
    delete _effect;
}

void EffectPlayer::start(const EffectParams& p) {
    stop();
    delete _effect;
    _params = p;
    switch (p.effectId) {
        case 2:  _effect = new AndroidEffect(); break;
        default: _effect = new SolidEffect();   break;
    }
    if (!_effect) return;  // OOM — better than crashing in reset()
    _effect->reset(p, _leds.numLeds());
    _taskRunning = true;
    xTaskCreatePinnedToCore(taskEntry, "effect", 4096, this, 4, &_taskHandle, 1);
}

void EffectPlayer::stop() {
    if (!_taskHandle) return;
    _taskRunning = false;
    while (_taskHandle) vTaskDelay(1);
    _leds.clear();
}

void EffectPlayer::setParams(const EffectParams& p) {
    _params = p;
}

void EffectPlayer::taskEntry(void* arg) {
    auto* ep = static_cast<EffectPlayer*>(arg);
    ep->runTask();
    ep->_taskHandle = nullptr;
    vTaskDelete(nullptr);
}

void EffectPlayer::runTask() {
    while (_taskRunning) {
        int64_t now = esp_timer_get_time();
        _effect->update(_leds, _params);
        int64_t nextUs    = now + (int64_t)_effect->intervalUs(_params);
        int64_t remaining = nextUs - esp_timer_get_time();
        if (remaining > 10000) {
            vTaskDelay(pdMS_TO_TICKS(remaining / 1000 - 5));
        } else {
            while (_taskRunning && esp_timer_get_time() < nextUs) {
                taskYIELD();
            }
        }
    }
}
