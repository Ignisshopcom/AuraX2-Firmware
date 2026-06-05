#include "app_config.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <string.h>
#include <ctype.h>

static bool gImportedFromWled = false;
static bool gConfigNeedsSave  = false;

static constexpr uint16_t AURAX_CONFIG_VERSION = 2;

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

static bool validApCode(const char* code) {
    if (!code || strlen(code) != 4) return false;
    for (int i = 0; i < 4; i++) {
        unsigned char ch = (unsigned char)code[i];
        if (!isxdigit(ch)) return false;
    }
    return true;
}

static bool ensureApCode(AppConfig& cfg) {
    if (validApCode(cfg.apCode)) {
        for (int i = 0; i < 4; i++) cfg.apCode[i] = (char)toupper((unsigned char)cfg.apCode[i]);
        cfg.apCode[4] = '\0';
        return false;
    }

    uint32_t rnd = esp_random() ^ (uint32_t)ESP.getEfuseMac();
    if ((rnd & 0xFFFFu) == 0) rnd ^= 0xA5A5u;
    snprintf(cfg.apCode, sizeof(cfg.apCode), "%04X", (unsigned)(rnd & 0xFFFFu));
    return true;
}

static bool isPlaceholderSsid(const char* ssid) {
    if (!ssid || !strlen(ssid)) return false;
    return strcasecmp(ssid, "Your_Network") == 0 ||
           strcasecmp(ssid, "Your_WiFi") == 0 ||
           strcasecmp(ssid, "YOUR_WIFI_SSID") == 0 ||
           strcasecmp(ssid, "ssid") == 0;
}

static bool isPlaceholderPassword(const char* password) {
    if (!password || !strlen(password)) return false;
    return strcasecmp(password, "Your_Password") == 0 ||
           strcasecmp(password, "YOUR_WIFI_PASSWORD") == 0 ||
           strcasecmp(password, "password") == 0;
}

static bool sanitizeWifiCredentials(AppConfig& cfg) {
    bool changed = false;
    if (isPlaceholderSsid(cfg.ssid)) {
        cfg.ssid[0] = '\0';
        cfg.password[0] = '\0';
        changed = true;
    } else if (isPlaceholderPassword(cfg.password)) {
        cfg.password[0] = '\0';
        changed = true;
    }
    return changed;
}

static uint32_t fnv1aFileHash(const char* path) {
    if (!LittleFS.exists(path)) return 0;
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t hash = 2166136261u;
    while (f.available()) {
        uint8_t buf[128];
        size_t n = f.read(buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            hash ^= buf[i];
            hash *= 16777619u;
        }
    }
    f.close();
    return hash ? hash : 1;
}

static uint32_t wledConfigHash() {
    uint32_t cfgHash = fnv1aFileHash("/cfg.json");
    if (!cfgHash) return 0;
    uint32_t secHash = fnv1aFileHash("/wsec.json");
    return cfgHash ^ (secHash << 7) ^ (secHash >> 25) ^ 0xA20F2D1Du;
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
    if (interval >= 100) {
        cfg.batIntervalMs = interval < BATTERY_MIN_INTERVAL_MS ? BATTERY_MIN_INTERVAL_MS : interval;
    }

    JsonObject autoOff = battery["auto-off"];
    if (!autoOff.isNull()) {
        // Keep WLED voltage calibration, but do not enable AuraX auto-off during migration.
        // On clean/test boards the ADC pin may float and would otherwise blackout immediately.
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
    if (cfg.effectSpeed > 255) cfg.effectSpeed = 255;
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
    if (cfg.batIntervalMs < BATTERY_MIN_INTERVAL_MS) cfg.batIntervalMs = BATTERY_MIN_INTERVAL_MS;
    normalizeHostname(cfg.hostname, sizeof(cfg.hostname));
}

static uint8_t firstSyncChannel(uint16_t mask) {
    for (uint8_t ch = 1; ch <= 10; ch++) {
        if (mask & (1u << (ch - 1))) return ch;
    }
    return 0;
}

bool saveConfig(const AppConfig& cfg);  // forward decl — defined below

static bool isWledDataClockType(int type) {
    return type >= 48 && type <= 63;
}

static bool isWledDataOnlyType(int type) {
    return type >= 16 && type <= 31;
}

static bool isWledUsableLedType(int type) {
    return isWledDataOnlyType(type) || isWledDataClockType(type);
}

static void importFromWled(AppConfig& cfg) {
    if (!LittleFS.exists("/cfg.json")) return;

    DynamicJsonDocument filter(1536);
    filter["nw"]["ins"][0]["ssid"]          = true;
    filter["nw"]["ins"][0]["psk"]           = true;
    filter["hw"]["led"]["total"]            = true;
    filter["hw"]["led"]["rev"]              = true;
    for (int i = 0; i < 10; i++) {
        filter["hw"]["led"]["ins"][i]["start"]  = true;
        filter["hw"]["led"]["ins"][i]["len"]    = true;
        filter["hw"]["led"]["ins"][i]["type"]   = true;
        filter["hw"]["led"]["ins"][i]["pin"][0] = true;
        filter["hw"]["led"]["ins"][i]["pin"][1] = true;
        filter["hw"]["led"]["ins"][i]["rev"]    = true;
        filter["hw"]["led"]["ins"][i]["maxpwr"] = true;
    }
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

    DynamicJsonDocument doc(4096);
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

    JsonArrayConst buses = doc["hw"]["led"]["ins"].as<JsonArrayConst>();
    JsonObjectConst selectedBus;
    uint32_t summedLedCount = 0;
    int selectedType = -1;
    int selectedDataPin = -1;
    int selectedClockPin = -1;
    int selectedMaxPower = -1;
    bool selectedReverse = false;

    for (JsonObjectConst bus : buses) {
        int type = bus["type"] | 22;  // WLED defaults missing bus type to WS281x RGB.
        uint16_t len = bus["len"] | 0;
        if (len > 0 && summedLedCount + len <= 2048) summedLedCount += len;

        JsonArrayConst pins = bus["pin"].as<JsonArrayConst>();
        int dataPin = pins[0] | -1;
        if (selectedBus.isNull() && len > 0 && dataPin >= 0 && dataPin <= 48 && isWledUsableLedType(type)) {
            selectedBus = bus;
            selectedType = type;
            selectedDataPin = dataPin;
            selectedClockPin = pins[1] | -1;
            selectedMaxPower = bus["maxpwr"] | -1;
            selectedReverse = bus["rev"] | false;
        }
    }

    uint16_t wledTotal = doc["hw"]["led"]["total"] | 0;
    uint16_t importedCount = 0;
    if (!selectedBus.isNull()) importedCount = selectedBus["len"] | 0;
    if (importedCount == 0 && summedLedCount > 0 && summedLedCount <= 2048) importedCount = (uint16_t)summedLedCount;
    if (importedCount == 0) importedCount = wledTotal;
    if (importedCount >= 1 && importedCount <= 2048) cfg.numLeds = importedCount;

    if (!selectedBus.isNull()) {
        cfg.ledType = isWledDataClockType(selectedType) ? LED_TYPE_APA102 : LED_TYPE_WS281X;
        cfg.dataPin = (uint8_t)selectedDataPin;
        if (cfg.ledType == LED_TYPE_APA102 && selectedClockPin >= 0 && selectedClockPin <= 48) {
            cfg.clkPin = (uint8_t)selectedClockPin;
        }
        cfg.effectReverse = (selectedReverse || (doc["hw"]["led"]["rev"] | false)) ? 1 : 0;
        LOG("[cfg] WLED LED bus: type=%d len=%u total=%u data=%d clk=%d -> AuraX ledType=%u numLeds=%u data=%u clk=%u\n",
            selectedType, (unsigned)(selectedBus["len"] | 0), (unsigned)wledTotal,
            selectedDataPin, selectedClockPin, cfg.ledType, cfg.numLeds, cfg.dataPin, cfg.clkPin);
    } else if (importedCount >= 1) {
        LOG("[cfg] WLED LED count imported without valid bus: total=%u summed=%u -> numLeds=%u\n",
            (unsigned)wledTotal, (unsigned)summedLedCount, cfg.numLeds);
    }

    int maxpwr = selectedMaxPower;
    if (maxpwr < 0) maxpwr = doc["hw"]["led"]["maxpwr"] | -1;
    if (maxpwr >= 0) cfg.mALimit = (uint16_t)maxpwr;

    const char* mdns = doc["id"]["mdns"] | "";
    const char* name = doc["id"]["name"] | "";
    if (strlen(mdns) && strcmp(mdns, "x") != 0 && strcasecmp(mdns, "wled") != 0)
        strlcpy(cfg.hostname, mdns, sizeof(cfg.hostname));
    else if (strlen(name) && strcasecmp(name, "wled") != 0)
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
    cfg.effectSpeed     = 128;
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
    cfg.batAutoOff          = 0;
    cfg.batAutoOffThreshold = 10;
    cfg.syncEnabled         = 1;
    cfg.syncMask            = 1;
    cfg.wledImportHash      = 0;
    cfg.ledType = LED_TYPE;
    cfg.numLeds = NUM_LEDS;
    cfg.dataPin = (LED_TYPE == LED_TYPE_APA102) ? LED_DATA_PIN : WS_DATA_PIN;
    cfg.clkPin  = LED_CLK_PIN;
    strncpy(cfg.ssid,     WIFI_SSID,     sizeof(cfg.ssid)     - 1);
    strncpy(cfg.password, WIFI_PASSWORD, sizeof(cfg.password) - 1);
    strncpy(cfg.pixFile,  PIX_FILE,      sizeof(cfg.pixFile)  - 1);
    ensureApCode(cfg);
    return cfg;
}

AppConfig defaultConfig() {
    AppConfig cfg = defaults();
    if (strlen(cfg.hostname) == 0)
        strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
    normalizeEffectConfig(cfg);
    return cfg;
}

AppConfig loadConfig() {
    gImportedFromWled = false;
    gConfigNeedsSave  = false;
    AppConfig cfg = defaults();
    if (!LittleFS.exists(CFG_FILE)) {
        uint32_t currentWledHash = wledConfigHash();
        bool hasWledConfig = currentWledHash != 0;
        importFromWled(cfg);
        cfg.wledImportHash = currentWledHash;
        if (strlen(cfg.hostname) == 0)
            strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
        sanitizeWifiCredentials(cfg);
        ensureApCode(cfg);
        normalizeEffectConfig(cfg);
        if (hasWledConfig) {
            gImportedFromWled = true;
            gConfigNeedsSave = true;
            LOGLN("[cfg] WLED import kept in RAM; /config.json will be saved after WiFi starts");
        } else {
            gConfigNeedsSave = true;
            LOGLN("[cfg] defaults kept in RAM; /config.json will be saved after WiFi starts");
        }
        return cfg;
    }
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f) return cfg;

    StaticJsonDocument<2048> doc;
    bool shouldSave = false;
    uint16_t savedConfigVersion = 0;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        savedConfigVersion = doc["auraxConfigVersion"] | 0;
        cfg.ledType = doc["ledType"] | cfg.ledType;
        cfg.numLeds = doc["numLeds"] | cfg.numLeds;
        cfg.dataPin = doc["dataPin"] | cfg.dataPin;
        cfg.clkPin  = doc["clkPin"]  | cfg.clkPin;
        strlcpy(cfg.ssid,     doc["ssid"]     | cfg.ssid,     sizeof(cfg.ssid));
        strlcpy(cfg.password, doc["password"] | cfg.password, sizeof(cfg.password));
        strlcpy(cfg.apCode, doc["apCode"] | cfg.apCode, sizeof(cfg.apCode));
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
        cfg.wledImportHash      = doc["wledImportHash"]      | cfg.wledImportHash;
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
    uint32_t currentWledHash = wledConfigHash();
    if (currentWledHash && cfg.wledImportHash != currentWledHash) {
        importFromWled(cfg);
        cfg.wledImportHash = currentWledHash;
        gImportedFromWled = true;
        shouldSave = true;
        LOGLN("[cfg] WLED config changed; merged /cfg.json into AuraX settings");
    } else if (savedConfigVersion < AURAX_CONFIG_VERSION && LittleFS.exists("/cfg.json")) {
        cfg.wledImportHash = currentWledHash;
        shouldSave = true;
        LOGLN("[cfg] older AuraX config marked as migrated");
    }
    if (strlen(cfg.hostname) == 0)
        strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
    if (strcasecmp(cfg.hostname, "wled") == 0) {
        strlcpy(cfg.hostname, "aurax", sizeof(cfg.hostname));
        if (cfg.batAutoOff) cfg.batAutoOff = 0;
        shouldSave = true;
    }
    shouldSave = sanitizeWifiCredentials(cfg) || shouldSave;
    shouldSave = ensureApCode(cfg) || shouldSave;
    normalizeEffectConfig(cfg);
    if (shouldSave) {
        gConfigNeedsSave = true;
        LOGLN("[cfg] config normalized in RAM; /config.json will be saved after WiFi starts");
    }
    return cfg;
}

bool configImportedFromWled() {
    return gImportedFromWled;
}

bool configNeedsSave() {
    return gConfigNeedsSave;
}

bool saveConfig(const AppConfig& cfg) {
    StaticJsonDocument<2048> doc;
    doc["auraxConfigVersion"] = AURAX_CONFIG_VERSION;
    doc["ledType"]  = cfg.ledType;
    doc["numLeds"]  = cfg.numLeds;
    doc["dataPin"]  = cfg.dataPin;
    doc["clkPin"]   = cfg.clkPin;
    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;
    doc["apCode"]   = cfg.apCode;
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
    doc["wledImportHash"]      = cfg.wledImportHash;
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
