#pragma once
#include <Arduino.h>
#include "app_config.h"

class BatteryMonitor {
public:
    void begin(AppConfig& cfg) { _cfg = &cfg; }

    // Call from handle(). Returns true once when battery first drops below
    // the auto-off threshold (caller should blackout LEDs).
    bool update() {
        if (!_cfg) return false;
        uint32_t now = millis();
        if (_lastMs != 0 && now - _lastMs < _cfg->batIntervalMs) return false;

        float raw = (analogReadMilliVolts(_cfg->batPin) + _cfg->batCalibration * 1000.0f)
                    * _cfg->batMultiplier;
        if (_lastMs == 0)
            _mvf = raw;
        else
            _mvf += 0.05f * (raw - _mvf);
        _lastMs = now;
        _mv = (uint16_t)(_mvf < 0 ? 0 : _mvf > 65535 ? 65535 : _mvf);

        if (_mv >= _cfg->batMaxMv || _cfg->batMaxMv <= _cfg->batMinMv) _pct = 100;
        else if (_mv <= _cfg->batMinMv) _pct = 0;
        else _pct = (uint8_t)(((uint32_t)(_mv - _cfg->batMinMv) * 100) /
                               (_cfg->batMaxMv - _cfg->batMinMv));

        bool low = _cfg->batAutoOff && _pct <= _cfg->batAutoOffThreshold;
        if (low && !_autoOffActive) {
            _autoOffActive = true;
            return true;  // trigger blackout
        }
        if (!low && _autoOffActive) _autoOffActive = false;  // recovered
        return false;
    }

    // Force immediate re-measurement on next update() call (e.g. after config change)
    void resetInterval() { _lastMs = 0; }

    uint16_t mv()  const { return _mv;  }
    uint8_t  pct() const { return _pct; }

private:
    AppConfig* _cfg          = nullptr;
    uint32_t   _lastMs       = 0;
    float      _mvf          = 0.0f;
    uint16_t   _mv           = 0;
    uint8_t    _pct          = 0;
    bool       _autoOffActive = false;
};
