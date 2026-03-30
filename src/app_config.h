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
};

// Load from /config.json — falls back to compile-time defaults if missing
AppConfig loadConfig();

// Save to /config.json — requires LittleFS to be mounted
bool saveConfig(const AppConfig& cfg);
