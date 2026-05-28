#include "app_config.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

bool saveConfig(const AppConfig& cfg);  // forward decl — defined below

static void importFromWled(AppConfig& cfg) {
    if (!LittleFS.exists("/cfg.json")) return;

    StaticJsonDocument<200> filter;
    filter["nw"]["ins"][0]["ssid"]          = true;
    filter["hw"]["led"]["ins"][0]["len"]    = true;
    filter["hw"]["led"]["ins"][0]["type"]   = true;
    filter["hw"]["led"]["ins"][0]["pin"][0] = true;
    filter["hw"]["led"]["ins"][0]["pin"][1] = true;
    filter["hw"]["led"]["maxpwr"]           = true;
    filter["id"]["mdns"]                    = true;
    filter["def"]["bri"]                    = true;

    StaticJsonDocument<512> doc;
    {
        File f = LittleFS.open("/cfg.json", "r");
        if (!f) return;
        DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
        f.close();
        if (err) return;
    }

    const char* ssid = doc["nw"]["ins"][0]["ssid"] | "";
    if (strlen(ssid)) strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));

    int wledType = doc["hw"]["led"]["ins"][0]["type"] | -1;
    if (wledType >= 0) {
        cfg.ledType = (wledType == 51) ? 1 : 0;
        cfg.numLeds = doc["hw"]["led"]["ins"][0]["len"] | cfg.numLeds;
        cfg.dataPin = doc["hw"]["led"]["ins"][0]["pin"][0] | cfg.dataPin;
        if (cfg.ledType == 1)
            cfg.clkPin = doc["hw"]["led"]["ins"][0]["pin"][1] | cfg.clkPin;
    }

    int maxpwr = doc["hw"]["led"]["maxpwr"] | -1;
    if (maxpwr >= 0) cfg.mALimit = (uint16_t)maxpwr;

    const char* mdns = doc["id"]["mdns"] | "";
    if (strlen(mdns) && strcmp(mdns, "x") != 0)
        strlcpy(cfg.hostname, mdns, sizeof(cfg.hostname));

    int wbri = doc["def"]["bri"] | -1;
    if (wbri >= 0)
        cfg.brightness = (wbri == 0) ? 0 : (uint8_t)((wbri * 100 + 127) / 255);

    if (LittleFS.exists("/wsec.json")) {
        StaticJsonDocument<64> wsecFilter;
        wsecFilter["nw"]["ins"][0]["psk"] = true;
        StaticJsonDocument<128> wsec;
        File wf = LittleFS.open("/wsec.json", "r");
        if (wf) {
            if (deserializeJson(wsec, wf, DeserializationOption::Filter(wsecFilter)) == DeserializationError::Ok) {
                const char* psk = wsec["nw"]["ins"][0]["psk"] | "";
                if (strlen(psk)) strlcpy(cfg.password, psk, sizeof(cfg.password));
            }
            wf.close();
        }
    }

    LOGLN("[cfg] imported settings from WLED /cfg.json");
}

static AppConfig defaults() {
    AppConfig cfg = {};
    cfg.tempo        = 100;
    cfg.endBehavior  = 255;
    cfg.effectId      = 1;
    cfg.effectSpeed   = 100;
    cfg.effectDotSize = 3;
    cfg.paletteSize   = 1;
    cfg.paletteR[0]   = 255;
    cfg.batPin              = 8;
    cfg.batMultiplier       = 2.904f;
    cfg.batCalibration      = 0.344f;
    cfg.batMinMv            = 3000;
    cfg.batMaxMv            = 4200;
    cfg.batIntervalMs       = 30000;
    cfg.batAutoOff          = 0;
    cfg.batAutoOffThreshold = 10;
    cfg.syncChannel         = 0;
    cfg.ledType = LED_TYPE;
    cfg.numLeds = NUM_LEDS;
    cfg.dataPin = (LED_TYPE == LED_TYPE_APA102) ? LED_DATA_PIN : WS_DATA_PIN;
    cfg.clkPin  = LED_CLK_PIN;
    cfg.wifiMode = WIFI_MODE_NORMAL;
    strncpy(cfg.ssid,     WIFI_SSID,     sizeof(cfg.ssid)     - 1);
    strncpy(cfg.password, WIFI_PASSWORD, sizeof(cfg.password) - 1);
    strncpy(cfg.groupSsid, GROUP_WIFI_SSID, sizeof(cfg.groupSsid) - 1);
    strncpy(cfg.groupPassword, GROUP_WIFI_PASSWORD, sizeof(cfg.groupPassword) - 1);
    strncpy(cfg.pixFile,  PIX_FILE,      sizeof(cfg.pixFile)  - 1);
    return cfg;
}

AppConfig loadConfig() {
    AppConfig cfg = defaults();
    if (!LittleFS.exists(CFG_FILE)) {
        importFromWled(cfg);
        if (strlen(cfg.hostname) == 0)
            strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
        saveConfig(cfg);
        return cfg;
    }
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f) return cfg;

    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        cfg.ledType = doc["ledType"] | cfg.ledType;
        cfg.numLeds = doc["numLeds"] | cfg.numLeds;
        cfg.dataPin = doc["dataPin"] | cfg.dataPin;
        cfg.clkPin  = doc["clkPin"]  | cfg.clkPin;
        cfg.wifiMode = doc["wifiMode"] | cfg.wifiMode;
        if (cfg.wifiMode > WIFI_MODE_GROUP_CLIENT) cfg.wifiMode = WIFI_MODE_NORMAL;
        strlcpy(cfg.ssid,     doc["ssid"]     | cfg.ssid,     sizeof(cfg.ssid));
        strlcpy(cfg.password, doc["password"] | cfg.password, sizeof(cfg.password));
        strlcpy(cfg.groupSsid, doc["groupSsid"] | cfg.groupSsid, sizeof(cfg.groupSsid));
        strlcpy(cfg.groupPassword, doc["groupPassword"] | cfg.groupPassword, sizeof(cfg.groupPassword));
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
        cfg.batPin              = doc["batPin"]              | cfg.batPin;
        cfg.batMultiplier       = doc["batMultiplier"]       | cfg.batMultiplier;
        cfg.batCalibration      = doc["batCalibration"]      | cfg.batCalibration;
        cfg.batMinMv            = doc["batMinMv"]            | cfg.batMinMv;
        cfg.batMaxMv            = doc["batMaxMv"]            | cfg.batMaxMv;
        cfg.batIntervalMs       = doc["batIntervalMs"]       | cfg.batIntervalMs;
        cfg.batAutoOff          = doc["batAutoOff"]          | cfg.batAutoOff;
        cfg.batAutoOffThreshold = doc["batAutoOffThreshold"] | cfg.batAutoOffThreshold;
        cfg.syncChannel         = doc["syncChannel"]         | cfg.syncChannel;
        cfg.autoStart           = doc["autoStart"]           | 0;
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
    if (strlen(cfg.groupSsid) == 0)
        strlcpy(cfg.groupSsid, GROUP_WIFI_SSID, sizeof(cfg.groupSsid));
    if (strlen(cfg.groupPassword) > 0 && strlen(cfg.groupPassword) < 8)
        strlcpy(cfg.groupPassword, GROUP_WIFI_PASSWORD, sizeof(cfg.groupPassword));
    return cfg;
}

bool saveConfig(const AppConfig& cfg) {
    StaticJsonDocument<1536> doc;
    doc["ledType"]  = cfg.ledType;
    doc["numLeds"]  = cfg.numLeds;
    doc["dataPin"]  = cfg.dataPin;
    doc["clkPin"]   = cfg.clkPin;
    doc["wifiMode"] = cfg.wifiMode;
    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;
    doc["groupSsid"] = cfg.groupSsid;
    doc["groupPassword"] = cfg.groupPassword;
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
    doc["batPin"]              = cfg.batPin;
    doc["batMultiplier"]       = cfg.batMultiplier;
    doc["batCalibration"]      = cfg.batCalibration;
    doc["batMinMv"]            = cfg.batMinMv;
    doc["batMaxMv"]            = cfg.batMaxMv;
    doc["batIntervalMs"]       = cfg.batIntervalMs;
    doc["batAutoOff"]          = cfg.batAutoOff;
    doc["batAutoOffThreshold"] = cfg.batAutoOffThreshold;
    doc["syncChannel"]         = cfg.syncChannel;
    doc["autoStart"]           = cfg.autoStart;
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
