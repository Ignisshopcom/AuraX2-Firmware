#pragma once
#include <stdint.h>

#define CFG_FILE "/config.json"

struct AppConfig {
    uint8_t  ledType;       // 0=APA102, 1=WS281x
    uint16_t numLeds;
    uint8_t  dataPin;       // MOSI for APA102, DATA for WS281x
    uint8_t  clkPin;        // CLK for APA102
    char     ssid[64];
    char     password[64];
    char     pixFile[64];
    char     hostname[32];  // mDNS hostname bez .local; "" → auto z chip ID
};

// Load from /config.json — falls back to compile-time defaults if missing
AppConfig loadConfig();

// Save to /config.json — requires LittleFS to be mounted
bool saveConfig(const AppConfig& cfg);
