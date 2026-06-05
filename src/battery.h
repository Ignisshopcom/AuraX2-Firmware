#pragma once
#include <Arduino.h>
#include "app_config.h"
#include "config.h"

class BatteryMonitor {
public:
    void begin(AppConfig& cfg) { _cfg = &cfg; }

    // Call from handle(). Returns true once when battery first drops below
    // the auto-off threshold (caller should blackout LEDs).
    bool update() {
        if (!_cfg) return false;
        uint32_t now = millis();
        uint32_t intervalMs = _cfg->batIntervalMs;
        if (intervalMs < BATTERY_MIN_INTERVAL_MS) intervalMs = BATTERY_MIN_INTERVAL_MS;
        if (_lastMs != 0 && now - _lastMs < intervalMs) return false;

        if (batteryPinConflictsWithLed()) {
            if (!_pinConflictLogged) {
                LOG("[bat] ADC pin %u conflicts with LED output, battery read skipped\n", _cfg->batPin);
                _pinConflictLogged = true;
            }
            if (_lastMs == 0) {
                _mvf = 0.0f;
                _mv = 0;
                _pct = 0;
            }
            _lastMs = now;
            return false;
        }

        float voltage = (analogReadMilliVolts(_cfg->batPin) / 1000.0f) * _cfg->batMultiplier
                        + _cfg->batCalibration;
        _pinConflictLogged = false;
        float raw = voltage * 1000.0f;
        if (_lastMs == 0)
            _mvf = raw;
        else
            _mvf += 0.05f * (raw - _mvf);
        _lastMs = now;
        _mv = (uint16_t)(_mvf < 0 ? 0 : _mvf > 65535 ? 65535 : _mvf);

        _pct = lipoPercent(_mv, _cfg->batMinMv, _cfg->batMaxMv);

        bool validReading = (_mv >= 1000 && _mv <= 6500);
        bool low = validReading && _cfg->batAutoOff && _pct <= _cfg->batAutoOffThreshold;
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
    bool batteryPinConflictsWithLed() const {
        if (!_cfg) return true;
        if (_cfg->batPin > 48) return true;
        if (_cfg->batPin == _cfg->dataPin) return true;
        if (_cfg->ledType == LED_TYPE_APA102 && _cfg->batPin == _cfg->clkPin) return true;
        return false;
    }

    static uint8_t lipoPercent(uint16_t mv, uint16_t minMv, uint16_t maxMv) {
        if (maxMv <= minMv) return 100;

        float level = ((float)mv - (float)minMv) * 100.0f / ((float)maxMv - (float)minMv);
        if (level < 40.0f) {
            level = level * 12.0f / 40.0f;
        } else if (level < 90.0f) {
            level = 12.0f + (level - 40.0f) * 83.0f / 50.0f;
        } else {
            level = 95.0f + (level - 90.0f) * 5.0f / 15.0f;
        }

        if (level < 0.0f) return 0;
        if (level > 100.0f) return 100;
        return (uint8_t)level;
    }

    AppConfig* _cfg          = nullptr;
    uint32_t   _lastMs       = 0;
    float      _mvf          = 0.0f;
    uint16_t   _mv           = 0;
    uint8_t    _pct          = 0;
    bool       _autoOffActive = false;
    bool       _pinConflictLogged = false;
};
