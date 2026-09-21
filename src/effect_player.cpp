#include "effect_player.h"
#include "solid_effect.h"
#include "android_effect.h"
#include "wled_fx_effect.h"
#include "task_compat.h"
#include <esp_timer.h>

EffectPlayer::EffectPlayer(ILedDriver& leds) : _leds(leds) {}

EffectPlayer::~EffectPlayer() {
    stop();
    delete _effect;
}

static uint8_t effectFamily(uint8_t effectId) {
    if (effectId == EFFECT_SOLID) return 1;
    if (effectId == EFFECT_ANDROID) return 2;
    if (effectId == EFFECT_AUDIO_REACTIVE) return 4;
    return 3;
}

void EffectPlayer::start(const EffectParams& p) {
    stop();
    delete _effect;
    _params = p;
    switch (p.effectId) {
        case EFFECT_AUDIO_REACTIVE: _effect = new AudioReactiveEffect(_audioInput); break;
        case EFFECT_ANDROID: _effect = new AndroidEffect(); break;
        case EFFECT_SOLID:   _effect = new SolidEffect();   break;
        default:             _effect = new WledFxEffect();  break;
    }
    if (!_effect) return;  // OOM — better than crashing in reset()
    _effect->reset(p, _leds.logicalNumLeds());
    if (p.effectId == EFFECT_AUDIO_REACTIVE && !static_cast<AudioReactiveEffect*>(_effect)->ready()) return;
    _fpsWindowFrames = 0;
    _fpsWindowStartUs = esp_timer_get_time();
    _currentFpsX10 = 0;
    _taskRunning = true;
    xTaskCreatePinnedToCore(taskEntry, "effect", 4096, this, AURAX_EFFECT_TASK_PRIORITY, &_taskHandle, AURAX_LED_TASK_CORE);
}

void EffectPlayer::apply(const EffectParams& p) {
    EffectParams current = {};
    portENTER_CRITICAL(&_paramsMux);
    current = _params;
    portEXIT_CRITICAL(&_paramsMux);

    if (!_taskHandle || !_effect || current.effectId != p.effectId ||
        effectFamily(current.effectId) != effectFamily(p.effectId)) {
        start(p);
        return;
    }
    setParams(p);
}

void EffectPlayer::stop() {
    ++_controlRevision;
    if (!_taskHandle) return;
    _taskRunning = false;
    while (_taskHandle) vTaskDelay(1);
    _currentFpsX10 = 0;
    _fpsWindowFrames = 0;
    _leds.clear();
}

void EffectPlayer::setParams(const EffectParams& p) {
    ++_controlRevision;
    portENTER_CRITICAL(&_paramsMux);
    _params = p;
    portEXIT_CRITICAL(&_paramsMux);
}

bool EffectPlayer::isAudioReactive() {
    EffectParams current = {};
    portENTER_CRITICAL(&_paramsMux);
    current = _params;
    portEXIT_CRITICAL(&_paramsMux);
    return _taskHandle != nullptr && current.effectId == EFFECT_AUDIO_REACTIVE;
}

void EffectPlayer::updateAudioReactive(uint8_t volume, uint8_t bass, uint8_t mid,
                                       uint8_t treble, uint8_t beat) {
    _audioInput.update(volume, bass, mid, treble, beat);
}

void EffectPlayer::clearAudioReactive() {
    _audioInput.clear();
}

void EffectPlayer::taskEntry(void* arg) {
    auto* ep = static_cast<EffectPlayer*>(arg);
    ep->runTask();
    ep->_taskHandle = nullptr;
    vTaskDelete(nullptr);
}

void EffectPlayer::runTask() {
    int64_t nextUs = esp_timer_get_time();
    while (_taskRunning) {
        EffectParams p = {};
        portENTER_CRITICAL(&_paramsMux);
        p = _params;
        portEXIT_CRITICAL(&_paramsMux);

        _effect->update(_leds, p);
        _fpsWindowFrames++;

        int64_t afterUpdateUs = esp_timer_get_time();
        int64_t fpsElapsedUs = afterUpdateUs - _fpsWindowStartUs;
        if (fpsElapsedUs >= 1000000LL) {
            _currentFpsX10 = (uint16_t)((_fpsWindowFrames * 10000000ULL + (uint64_t)fpsElapsedUs / 2) / (uint64_t)fpsElapsedUs);
            _fpsWindowFrames = 0;
            _fpsWindowStartUs = afterUpdateUs;
        }

        int64_t intervalUs = (int64_t)_effect->intervalUs(p);
        uint16_t maxHz = _leds.maxRefreshHz();
        if (maxHz > 0) {
            int64_t minIntervalUs = 1000000LL / (int64_t)maxHz;
            if (intervalUs < minIntervalUs) intervalUs = minIntervalUs;
        }
        nextUs += intervalUs;
        if (nextUs < afterUpdateUs) {
            nextUs = afterUpdateUs + intervalUs;
        }

#if defined(ARDUINO_ARCH_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_FREERTOS_UNICORE)
        vTaskDelay(1);
#else
        int64_t remaining = nextUs - esp_timer_get_time();
        if (remaining > 3000) {
            vTaskDelay(pdMS_TO_TICKS((remaining - 1500) / 1000));
        } else {
            while (_taskRunning && esp_timer_get_time() < nextUs) {
                taskYIELD();
            }
        }
#endif
    }
}
