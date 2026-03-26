#include "app_config.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

static AppConfig defaults() {
    AppConfig cfg = {};
    cfg.ledType = LED_TYPE;
    cfg.numLeds = NUM_LEDS;
    cfg.dataPin = (LED_TYPE == 0) ? LED_DATA_PIN : WS_DATA_PIN;
    cfg.clkPin  = LED_CLK_PIN;
    strncpy(cfg.ssid,     WIFI_SSID,     sizeof(cfg.ssid)     - 1);
    strncpy(cfg.password, WIFI_PASSWORD, sizeof(cfg.password) - 1);
    strncpy(cfg.pixFile,  PIX_FILE,      sizeof(cfg.pixFile)  - 1);
    return cfg;
}

AppConfig loadConfig() {
    AppConfig cfg = defaults();
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f) return cfg;

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        cfg.ledType = doc["ledType"] | cfg.ledType;
        cfg.numLeds = doc["numLeds"] | cfg.numLeds;
        cfg.dataPin = doc["dataPin"] | cfg.dataPin;
        cfg.clkPin  = doc["clkPin"]  | cfg.clkPin;
        strlcpy(cfg.ssid,     doc["ssid"]     | cfg.ssid,     sizeof(cfg.ssid));
        strlcpy(cfg.password, doc["password"] | cfg.password, sizeof(cfg.password));
        strlcpy(cfg.pixFile,  doc["pixFile"]  | cfg.pixFile,  sizeof(cfg.pixFile));
        strlcpy(cfg.hostname, doc["hostname"] | "",            sizeof(cfg.hostname));
    }
    f.close();
    if (strlen(cfg.hostname) == 0)
        strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
    return cfg;
}

bool saveConfig(const AppConfig& cfg) {
    StaticJsonDocument<512> doc;
    doc["ledType"]  = cfg.ledType;
    doc["numLeds"]  = cfg.numLeds;
    doc["dataPin"]  = cfg.dataPin;
    doc["clkPin"]   = cfg.clkPin;
    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;
    doc["pixFile"]  = cfg.pixFile;
    doc["hostname"] = cfg.hostname;

    File f = LittleFS.open(CFG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}
