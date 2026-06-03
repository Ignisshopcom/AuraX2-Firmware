#include "effect_player.h"
#include "solid_effect.h"
#include "android_effect.h"
#include "wled_fx_effect.h"
#include <esp_timer.h>

EffectPlayer::EffectPlayer(ILedDriver& leds) : _leds(leds) {}

EffectPlayer::~EffectPlayer() {
    stop();
    delete _effect;
}

static uint8_t effectFamily(uint8_t effectId) {
    if (effectId == EFFECT_SOLID) return 1;
    if (effectId == EFFECT_ANDROID) return 2;
    return 3;
}

void EffectPlayer::start(const EffectParams& p) {
    stop();
    delete _effect;
    _params = p;
    switch (p.effectId) {
        case EFFECT_ANDROID: _effect = new AndroidEffect(); break;
        case EFFECT_SOLID:   _effect = new SolidEffect();   break;
        default:             _effect = new WledFxEffect();  break;
    }
    if (!_effect) return;  // OOM — better than crashing in reset()
    _effect->reset(p, _leds.numLeds());
    _fpsWindowFrames = 0;
    _fpsWindowStartUs = esp_timer_get_time();
    _currentFpsX10 = 0;
    _taskRunning = true;
    xTaskCreatePinnedToCore(taskEntry, "effect", 4096, this, 4, &_taskHandle, 1);
}

void EffectPlayer::apply(const EffectParams& p) {
    if (!_taskHandle || !_effect || effectFamily(_params.effectId) != effectFamily(p.effectId)) {
        start(p);
        return;
    }
    setParams(p);
}

void EffectPlayer::stop() {
    if (!_taskHandle) return;
    _taskRunning = false;
    while (_taskHandle) vTaskDelay(1);
    _currentFpsX10 = 0;
    _fpsWindowFrames = 0;
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
        EffectParams p = _params;
        int64_t now = esp_timer_get_time();
        _effect->update(_leds, p);
        _fpsWindowFrames++;
        int64_t fpsElapsedUs = now - _fpsWindowStartUs;
        if (fpsElapsedUs >= 1000000LL) {
            _currentFpsX10 = (uint16_t)((_fpsWindowFrames * 10000000ULL + (uint64_t)fpsElapsedUs / 2) / (uint64_t)fpsElapsedUs);
            _fpsWindowFrames = 0;
            _fpsWindowStartUs = now;
        }
        int64_t nextUs    = now + (int64_t)_effect->intervalUs(p);
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
