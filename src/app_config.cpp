#include "app_config.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

static AppConfig defaults() {
    AppConfig cfg = {};
    cfg.tempo        = 100;
    cfg.endBehavior  = 255;
    cfg.effectId      = 1;
    cfg.effectSpeed   = 100;
    cfg.effectDotSize = 3;
    cfg.paletteSize   = 1;
    cfg.paletteR[0]   = 255;
    cfg.mAPerLed      = 60;
    cfg.batPin              = 8;
    cfg.batMultiplier       = 2.904f;
    cfg.batCalibration      = 0.344f;
    cfg.batMinMv            = 3000;
    cfg.batMaxMv            = 4200;
    cfg.batCapacityMah      = 1000;
    cfg.batIntervalMs       = 30000;
    cfg.batAutoOff          = 0;
    cfg.batAutoOffThreshold = 10;
    cfg.ledType = LED_TYPE;
    cfg.numLeds = NUM_LEDS;
    cfg.dataPin = (LED_TYPE == LED_TYPE_APA102) ? LED_DATA_PIN : WS_DATA_PIN;
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

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        cfg.ledType = doc["ledType"] | cfg.ledType;
        cfg.numLeds = doc["numLeds"] | cfg.numLeds;
        cfg.dataPin = doc["dataPin"] | cfg.dataPin;
        cfg.clkPin  = doc["clkPin"]  | cfg.clkPin;
        strlcpy(cfg.ssid,     doc["ssid"]     | cfg.ssid,     sizeof(cfg.ssid));
        strlcpy(cfg.password, doc["password"] | cfg.password, sizeof(cfg.password));
        strlcpy(cfg.pixFile,  doc["pixFile"]  | cfg.pixFile,  sizeof(cfg.pixFile));
        strlcpy(cfg.hostname, doc["hostname"] | "",            sizeof(cfg.hostname));
        cfg.brightness    = doc["brightness"]    | cfg.brightness;
        cfg.tempo         = doc["tempo"]         | cfg.tempo;
        cfg.endBehavior   = doc["endBehavior"]   | cfg.endBehavior;
        cfg.effectId      = doc["effectId"]      | cfg.effectId;
        cfg.effectSpeed   = doc["effectSpeed"]   | cfg.effectSpeed;
        cfg.effectDotSize = doc["effectDotSize"] | cfg.effectDotSize;
        cfg.paletteSize   = doc["paletteSize"]   | cfg.paletteSize;
        cfg.mALimit   = doc["mALimit"]   | cfg.mALimit;
        cfg.mAPerLed  = doc["mAPerLed"]  | cfg.mAPerLed;
        cfg.batPin              = doc["batPin"]              | cfg.batPin;
        cfg.batMultiplier       = doc["batMultiplier"]       | cfg.batMultiplier;
        cfg.batCalibration      = doc["batCalibration"]      | cfg.batCalibration;
        cfg.batMinMv            = doc["batMinMv"]            | cfg.batMinMv;
        cfg.batMaxMv            = doc["batMaxMv"]            | cfg.batMaxMv;
        cfg.batCapacityMah      = doc["batCapacityMah"]      | cfg.batCapacityMah;
        cfg.batIntervalMs       = doc["batIntervalMs"]       | cfg.batIntervalMs;
        cfg.batAutoOff          = doc["batAutoOff"]          | cfg.batAutoOff;
        cfg.batAutoOffThreshold = doc["batAutoOffThreshold"] | cfg.batAutoOffThreshold;
        JsonArray pR = doc["paletteR"], pG = doc["paletteG"], pB = doc["paletteB"];
        for (int i = 0; i < 4; i++) {
            if (i < (int)pR.size()) cfg.paletteR[i] = pR[i];
            if (i < (int)pG.size()) cfg.paletteG[i] = pG[i];
            if (i < (int)pB.size()) cfg.paletteB[i] = pB[i];
        }
    }
    f.close();
    if (strlen(cfg.hostname) == 0)
        strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
    return cfg;
}

bool saveConfig(const AppConfig& cfg) {
    StaticJsonDocument<1024> doc;
    doc["ledType"]  = cfg.ledType;
    doc["numLeds"]  = cfg.numLeds;
    doc["dataPin"]  = cfg.dataPin;
    doc["clkPin"]   = cfg.clkPin;
    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;
    doc["pixFile"]  = cfg.pixFile;
    doc["hostname"]   = cfg.hostname;
    doc["brightness"] = cfg.brightness;
    doc["tempo"]       = cfg.tempo;
    doc["endBehavior"] = cfg.endBehavior;
    doc["effectId"]      = cfg.effectId;
    doc["effectSpeed"]   = cfg.effectSpeed;
    doc["effectDotSize"] = cfg.effectDotSize;
    doc["paletteSize"]   = cfg.paletteSize;
    doc["mALimit"]  = cfg.mALimit;
    doc["mAPerLed"] = cfg.mAPerLed;
    doc["batPin"]              = cfg.batPin;
    doc["batMultiplier"]       = cfg.batMultiplier;
    doc["batCalibration"]      = cfg.batCalibration;
    doc["batMinMv"]            = cfg.batMinMv;
    doc["batMaxMv"]            = cfg.batMaxMv;
    doc["batCapacityMah"]      = cfg.batCapacityMah;
    doc["batIntervalMs"]       = cfg.batIntervalMs;
    doc["batAutoOff"]          = cfg.batAutoOff;
    doc["batAutoOffThreshold"] = cfg.batAutoOffThreshold;
    JsonArray pR = doc.createNestedArray("paletteR");
    JsonArray pG = doc.createNestedArray("paletteG");
    JsonArray pB = doc.createNestedArray("paletteB");
    for (int i = 0; i < 4; i++) { pR.add(cfg.paletteR[i]); pG.add(cfg.paletteG[i]); pB.add(cfg.paletteB[i]); }

    File f = LittleFS.open(CFG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}
