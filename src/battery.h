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
        _lastMs = now;

        int sum = 0;
        for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(_cfg->batPin);
        float mv = (sum / 8.0f + _cfg->batCalibration * 1000.0f) * _cfg->batMultiplier;
        _mv = (uint16_t)(mv < 0 ? 0 : mv > 65535 ? 65535 : mv);

        if (_mv >= _cfg->batMaxMv) _pct = 100;
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
    uint16_t   _mv           = 0;
    uint8_t    _pct          = 0;
    bool       _autoOffActive = false;
};
