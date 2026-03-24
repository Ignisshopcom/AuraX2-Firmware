#pragma once
#include <Arduino.h>

// XIAO ESP32-S3: battery voltage divider output on GPIO2 (A0), ratio 1:2
#define BAT_PIN 2

inline uint16_t batteryMillivolts() {
    int sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_PIN);
    return (sum / 8) * 2;  // ×2 for voltage divider
}

// LiPo: 4200 mV = 100 %, 3200 mV = 0 %
inline uint8_t batteryPercent(uint16_t mv) {
    if (mv >= 4200) return 100;
    if (mv <= 3200) return 0;
    return (uint8_t)((mv - 3200) * 100 / 1000);
}
