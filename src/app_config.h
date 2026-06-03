#pragma once
#include <stdint.h>
#include <stddef.h>

#define CFG_FILE "/config.json"

static constexpr uint8_t WIFI_MODE_NORMAL       = 0;
static constexpr uint8_t WIFI_MODE_GROUP_MASTER = 1;
static constexpr uint8_t WIFI_MODE_GROUP_CLIENT = 2;

struct AppConfig {
    uint8_t  ledType;       // 1=APA102, 0=WS281x  (LED_TYPE_APA102 / LED_TYPE_WS281X)
    uint16_t numLeds;
    uint8_t  dataPin;       // MOSI for APA102, DATA for WS281x
    uint8_t  clkPin;        // CLK for APA102
    uint8_t  wifiMode;      // 0=normal STA+fallback AP, 1=group AP, 2=group client
    char     ssid[64];
    char     password[64];
    char     groupSsid[32];
    char     groupPassword[64];
    char     apCode[8];      // fallback AP suffix, e.g. AuraX_AB12
    char     pixFile[64];
    char     hostname[32];  // device name / mDNS hostname without .local
    uint8_t  brightness;    // 0 = use per-pixel brightness, 1-100 = global override %
    uint16_t tempo;         // playback speed %; 100 = normal, 50 = half, 200 = double
    uint8_t  endBehavior;   // 255 = from .pix file, 0 = off, 1 = loop, 2 = keep
    // Effect settings
    uint8_t  effectId;        // 1=solid, 2=android, 10+ = AuraX/WLED-like effects
    uint16_t effectSpeed;     // 10-1000, 100=normal
    uint8_t  effectIntensity; // 0-255, effect-specific strength
    uint8_t  effectDotSize;   // effect-specific size/width
    uint8_t  effectPaletteId; // 0=custom color slots, 1+ = built-in palette
    uint8_t  effectReverse;   // 1 = render from the opposite LED end
    uint8_t  renderMirror;    // 1 = mirror rendering from the strip center
    uint8_t  paletteSize;     // 1-4
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
    // Sync classes. Bit 0 = class 1, bit 9 = class 10.
    uint8_t  syncEnabled; // 1 = ESP-NOW sync broadcasts/receives are active
    uint16_t syncMask;
    // Boot state: 0 = autoplay program, 1 = restore last effect
    uint8_t  autoStart;
    // Last imported WLED /cfg.json + /wsec.json fingerprint. Used to merge WLED changes after OTA.
    uint32_t wledImportHash;
};

// Load from /config.json; falls back to compile-time defaults if missing.
AppConfig loadConfig();

// Compile-time defaults only. Useful when LittleFS is not mounted yet.
AppConfig defaultConfig();

// True only after loadConfig() used WLED /cfg.json because AuraX /config.json was missing.
bool configImportedFromWled();

// True when loadConfig() normalized/imported settings that should be persisted later.
// Saving is intentionally delayed until WiFi/AP is already alive.
bool configNeedsSave();

// Save to /config.json; requires LittleFS to be mounted.
bool saveConfig(const AppConfig& cfg);

// Make user-entered device names safe for WiFi/mDNS hostnames.
void normalizeHostname(char* hostname, size_t len);
