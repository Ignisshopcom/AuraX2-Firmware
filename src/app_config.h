#pragma once
#include <stdint.h>

#define CFG_FILE "/config.json"

struct AppConfig {
    uint8_t  ledType;       // 1=APA102, 0=WS281x  (LED_TYPE_APA102 / LED_TYPE_WS281X)
    uint16_t numLeds;
    uint8_t  dataPin;       // MOSI for APA102, DATA for WS281x
    uint8_t  clkPin;        // CLK for APA102
    char     ssid[64];
    char     password[64];
    char     pixFile[64];
    char     hostname[32];  // mDNS hostname bez .local; "" → auto z chip ID
    uint8_t  brightness;    // 0 = use per-pixel brightness from .pix file, 1–100 = global override %
    uint16_t tempo;         // playback speed %; 100 = normal, 50 = half, 200 = double
    uint8_t  endBehavior;   // 255 = from .pix file, 0 = off, 1 = loop, 2 = keep
    // Effect settings
    uint8_t  effectId;      // 1=solid, 2=android
    uint16_t effectSpeed;   // 10–1000, 100=normální
    uint8_t  effectDotSize; // velikost tečky (android)
    uint8_t  paletteSize;   // 1–4
    uint8_t  paletteR[4];
    uint8_t  paletteG[4];
    uint8_t  paletteB[4];
    // Current limiting
    uint16_t mALimit;   // max mA total draw; 0 = no limit
    // Battery monitoring
    uint8_t  batPin;              // ADC pin
    float    batMultiplier;       // voltage divider ratio (e.g. 2.0 for 1:2 divider)
    float    batCalibration;      // offset correction in V
    uint16_t batMinMv;            // 0% threshold in mV
    uint16_t batMaxMv;            // 100% threshold in mV
    uint32_t batIntervalMs;       // measurement period in ms
    uint8_t  batAutoOff;          // 1 = enable auto-off when low
    uint8_t  batAutoOffThreshold; // auto-off threshold in %
    // Sync channel
    uint8_t  syncChannel;  // 1–10 = ESP-NOW sync group; 0 = sync disabled
    // Boot state: 0 = autoplay program, 1 = restore last effect
    uint8_t  autoStart;
};

// Load from /config.json — falls back to compile-time defaults if missing
AppConfig loadConfig();

// Save to /config.json — requires LittleFS to be mounted
bool saveConfig(const AppConfig& cfg);
