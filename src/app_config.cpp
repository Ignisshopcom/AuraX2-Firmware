#include "app_config.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>

void normalizeHostname(char* hostname, size_t len) {
    if (!hostname || len == 0) return;

    char out[32];
    size_t outLen = len < sizeof(out) ? len : sizeof(out);
    size_t j = 0;
    bool lastDash = false;

    for (size_t i = 0; hostname[i] && j + 1 < outLen; i++) {
        unsigned char ch = (unsigned char)hostname[i];
        if (isalnum(ch)) {
            out[j++] = (char)tolower(ch);
            lastDash = false;
        } else if ((ch == '-' || ch == '_' || isspace(ch)) && j > 0 && !lastDash) {
            out[j++] = '-';
            lastDash = true;
        }
    }

    while (j > 0 && out[j - 1] == '-') j--;
    if (j == 0) {
        strlcpy(hostname, "aurax", len);
        return;
    }
    out[j] = '\0';
    strlcpy(hostname, out, len);
}

static uint16_t voltsToMv(float volts, uint16_t fallback) {
    if (volts <= 0.0f || volts > 20.0f) return fallback;
    return (uint16_t)(volts * 1000.0f + 0.5f);
}

static void applyBatteryUsermod(JsonObject battery, AppConfig& cfg) {
    if (battery.isNull()) return;

    int pin = battery["pin"] | -1;
    if (pin >= 0 && pin <= 48) cfg.batPin = (uint8_t)pin;

    cfg.batMinMv = voltsToMv(battery["min-voltage"] | 0.0f, cfg.batMinMv);
    cfg.batMaxMv = voltsToMv(battery["max-voltage"] | 0.0f, cfg.batMaxMv);
    cfg.batMultiplier = battery["voltage-multiplier"] | cfg.batMultiplier;
    cfg.batCalibration = battery["calibration"] | cfg.batCalibration;

    uint32_t interval = battery["interval"] | cfg.batIntervalMs;
    if (interval >= 100) cfg.batIntervalMs = interval;

    JsonObject autoOff = battery["auto-off"];
    if (!autoOff.isNull()) {
        cfg.batAutoOff = (autoOff["enabled"] | (bool)cfg.batAutoOff) ? 1 : 0;
        int threshold = autoOff["threshold"] | cfg.batAutoOffThreshold;
        if (threshold < 0) threshold = 0;
        if (threshold > 100) threshold = 100;
        cfg.batAutoOffThreshold = (uint8_t)threshold;
    }
}

static void normalizeEffectConfig(AppConfig& cfg) {
    if (cfg.ledType != LED_TYPE_APA102 && cfg.ledType != LED_TYPE_WS281X) cfg.ledType = LED_TYPE_WS281X;
    if (cfg.numLeds < 1) cfg.numLeds = 1;
    if (cfg.numLeds > 2048) cfg.numLeds = 2048;
    if (cfg.dataPin > 48) cfg.dataPin = (cfg.ledType == LED_TYPE_APA102) ? LED_DATA_PIN : WS_DATA_PIN;
    if (cfg.clkPin > 48) cfg.clkPin = LED_CLK_PIN;
    if (cfg.effectSpeed < 10) cfg.effectSpeed = 10;
    if (cfg.effectSpeed > 1000) cfg.effectSpeed = 1000;
    if (cfg.effectDotSize < 1) cfg.effectDotSize = 1;
    if (cfg.effectDotSize > cfg.numLeds) {
        cfg.effectDotSize = cfg.numLeds > 255 ? 255 : (uint8_t)cfg.numLeds;
    }
    if (cfg.paletteSize < 1 || cfg.paletteSize > 4) cfg.paletteSize = 1;
    cfg.effectReverse = cfg.effectReverse ? 1 : 0;
    cfg.renderMirror = cfg.renderMirror ? 1 : 0;
    cfg.syncEnabled = cfg.syncEnabled ? 1 : 0;
    cfg.syncMask &= 0x03FF;
    if (cfg.batMaxMv <= cfg.batMinMv) {
        cfg.batMinMv = 3000;
        cfg.batMaxMv = 4200;
    }
    if (cfg.batIntervalMs < 100) cfg.batIntervalMs = 30000;
    normalizeHostname(cfg.hostname, sizeof(cfg.hostname));
}

static uint8_t firstSyncChannel(uint16_t mask) {
    for (uint8_t ch = 1; ch <= 10; ch++) {
        if (mask & (1u << (ch - 1))) return ch;
    }
    return 0;
}

bool saveConfig(const AppConfig& cfg);  // forward decl — defined below

static void importFromWled(AppConfig& cfg) {
    if (!LittleFS.exists("/cfg.json")) return;

    StaticJsonDocument<768> filter;
    filter["nw"]["ins"][0]["ssid"]          = true;
    filter["nw"]["ins"][0]["psk"]           = true;
    filter["hw"]["led"]["total"]            = true;
    filter["hw"]["led"]["rev"]              = true;
    filter["hw"]["led"]["ins"][0]["len"]    = true;
    filter["hw"]["led"]["ins"][0]["type"]   = true;
    filter["hw"]["led"]["ins"][0]["pin"][0] = true;
    filter["hw"]["led"]["ins"][0]["pin"][1] = true;
    filter["hw"]["led"]["ins"][0]["rev"]    = true;
    filter["hw"]["led"]["ins"][0]["maxpwr"] = true;
    filter["hw"]["led"]["maxpwr"]           = true;
    filter["id"]["name"]                    = true;
    filter["id"]["mdns"]                    = true;
    filter["def"]["bri"]                    = true;
    filter["um"]["Battery"]["pin"]          = true;
    filter["um"]["Battery"]["min-voltage"]  = true;
    filter["um"]["Battery"]["max-voltage"]  = true;
    filter["um"]["Battery"]["calibration"]  = true;
    filter["um"]["Battery"]["voltage-multiplier"] = true;
    filter["um"]["Battery"]["interval"]     = true;
    filter["um"]["Battery"]["auto-off"]["enabled"] = true;
    filter["um"]["Battery"]["auto-off"]["threshold"] = true;

    StaticJsonDocument<2048> doc;
    {
        File f = LittleFS.open("/cfg.json", "r");
        if (!f) return;
        DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
        f.close();
        if (err) return;
    }

    const char* ssid = doc["nw"]["ins"][0]["ssid"] | "";
    if (strlen(ssid)) strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    const char* pskInCfg = doc["nw"]["ins"][0]["psk"] | "";
    if (strlen(pskInCfg)) strlcpy(cfg.password, pskInCfg, sizeof(cfg.password));

    int wledType = doc["hw"]["led"]["ins"][0]["type"] | -1;
    if (wledType >= 0) {
        if (wledType == 51) {
            cfg.ledType = LED_TYPE_APA102;
        } else if (wledType >= 16 && wledType <= 39) {
            cfg.ledType = LED_TYPE_WS281X;
        }
        uint16_t count = doc["hw"]["led"]["ins"][0]["len"] | 0;
        if (count == 0) count = doc["hw"]["led"]["total"] | cfg.numLeds;
        cfg.numLeds = count;
        cfg.dataPin = doc["hw"]["led"]["ins"][0]["pin"][0] | cfg.dataPin;
        if (cfg.ledType == 1)
            cfg.clkPin = doc["hw"]["led"]["ins"][0]["pin"][1] | cfg.clkPin;
        cfg.effectReverse = (doc["hw"]["led"]["ins"][0]["rev"] | doc["hw"]["led"]["rev"] | false) ? 1 : 0;
    }

    int maxpwr = doc["hw"]["led"]["ins"][0]["maxpwr"] | -1;
    if (maxpwr < 0) maxpwr = doc["hw"]["led"]["maxpwr"] | -1;
    if (maxpwr >= 0) cfg.mALimit = (uint16_t)maxpwr;

    const char* mdns = doc["id"]["mdns"] | "";
    const char* name = doc["id"]["name"] | "";
    if (strlen(mdns) && strcmp(mdns, "x") != 0)
        strlcpy(cfg.hostname, mdns, sizeof(cfg.hostname));
    else if (strlen(name))
        strlcpy(cfg.hostname, name, sizeof(cfg.hostname));

    int wbri = doc["def"]["bri"] | -1;
    if (wbri >= 0)
        cfg.brightness = (wbri == 0) ? 0 : (uint8_t)((wbri * 100 + 127) / 255);

    applyBatteryUsermod(doc["um"]["Battery"].as<JsonObject>(), cfg);

    if (LittleFS.exists("/wsec.json")) {
        StaticJsonDocument<64> wsecFilter;
        wsecFilter["nw"]["ins"][0]["psk"] = true;
        StaticJsonDocument<256> wsec;
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
    cfg.effectId        = 1;
    cfg.effectSpeed     = 100;
    cfg.effectIntensity = 128;
    cfg.effectDotSize   = 3;
    cfg.effectPaletteId = 0;
    cfg.effectReverse   = 0;
    cfg.renderMirror    = 0;
    cfg.paletteSize   = 1;
    cfg.paletteR[0]   = 255;
    cfg.batPin              = 8;
    cfg.batMultiplier       = 2.904f;
    cfg.batCalibration      = 0.344f;
    cfg.batMinMv            = 3000;
    cfg.batMaxMv            = 4200;
    cfg.batIntervalMs       = 30000;
    cfg.batAutoOff          = 1;
    cfg.batAutoOffThreshold = 10;
    cfg.syncEnabled         = 0;
    cfg.syncMask            = 0;
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
        normalizeEffectConfig(cfg);
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
        const char* host = doc["deviceName"] | "";
        if (!strlen(host)) host = doc["hostname"] | "";
        strlcpy(cfg.hostname, host, sizeof(cfg.hostname));
        cfg.brightness    = doc["brightness"]    | cfg.brightness;
        cfg.tempo         = doc["tempo"]         | cfg.tempo;
        cfg.endBehavior   = doc["endBehavior"]   | cfg.endBehavior;
        cfg.effectId        = doc["effectId"]        | cfg.effectId;
        cfg.effectSpeed     = doc["effectSpeed"]     | cfg.effectSpeed;
        cfg.effectIntensity = doc["effectIntensity"] | cfg.effectIntensity;
        cfg.effectDotSize   = doc["effectDotSize"]   | cfg.effectDotSize;
        cfg.effectPaletteId = doc["effectPaletteId"] | cfg.effectPaletteId;
        cfg.effectReverse   = doc["effectReverse"]   | cfg.effectReverse;
        cfg.renderMirror    = doc["renderMirror"]    | cfg.renderMirror;
        cfg.paletteSize     = doc["paletteSize"]     | cfg.paletteSize;
        cfg.mALimit   = doc["mALimit"]   | cfg.mALimit;
        cfg.batPin              = doc["batPin"]              | cfg.batPin;
        cfg.batMultiplier       = doc["batMultiplier"]       | cfg.batMultiplier;
        cfg.batCalibration      = doc["batCalibration"]      | cfg.batCalibration;
        cfg.batMinMv            = doc["batMinMv"]            | cfg.batMinMv;
        cfg.batMaxMv            = doc["batMaxMv"]            | cfg.batMaxMv;
        cfg.batIntervalMs       = doc["batIntervalMs"]       | cfg.batIntervalMs;
        cfg.batAutoOff          = doc["batAutoOff"]          | cfg.batAutoOff;
        cfg.batAutoOffThreshold = doc["batAutoOffThreshold"] | cfg.batAutoOffThreshold;
        cfg.syncEnabled         = doc["syncEnabled"]         | cfg.syncEnabled;
        if (doc.containsKey("syncMask")) {
            cfg.syncMask = doc["syncMask"] | cfg.syncMask;
        } else {
            uint8_t ch = doc["syncChannel"] | 0;
            cfg.syncMask = (ch >= 1 && ch <= 10) ? (uint16_t)(1u << (ch - 1)) : 0;
        }
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
    normalizeEffectConfig(cfg);
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
    doc["deviceName"] = cfg.hostname;
    doc["hostname"]   = cfg.hostname;
    doc["brightness"] = cfg.brightness;
    doc["tempo"]       = cfg.tempo;
    doc["endBehavior"] = cfg.endBehavior;
    doc["effectId"]        = cfg.effectId;
    doc["effectSpeed"]     = cfg.effectSpeed;
    doc["effectIntensity"] = cfg.effectIntensity;
    doc["effectDotSize"]   = cfg.effectDotSize;
    doc["effectPaletteId"] = cfg.effectPaletteId;
    doc["effectReverse"]   = cfg.effectReverse;
    doc["renderMirror"]    = cfg.renderMirror;
    doc["paletteSize"]     = cfg.paletteSize;
    doc["mALimit"]  = cfg.mALimit;
    doc["batPin"]              = cfg.batPin;
    doc["batMultiplier"]       = cfg.batMultiplier;
    doc["batCalibration"]      = cfg.batCalibration;
    doc["batMinMv"]            = cfg.batMinMv;
    doc["batMaxMv"]            = cfg.batMaxMv;
    doc["batIntervalMs"]       = cfg.batIntervalMs;
    doc["batAutoOff"]          = cfg.batAutoOff;
    doc["batAutoOffThreshold"] = cfg.batAutoOffThreshold;
    doc["syncEnabled"]         = cfg.syncEnabled;
    doc["syncMask"]            = cfg.syncMask;
    doc["syncChannel"]         = firstSyncChannel(cfg.syncMask);
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
