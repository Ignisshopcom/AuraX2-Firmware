#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include "version.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <stdlib.h>
#include <string.h>
#include "web_html.h"

static constexpr uint16_t WLED_REALTIME_PORT = 21324;
static constexpr uint16_t DDP_REALTIME_PORT = 4048;
static constexpr uint16_t REALTIME_PACKET_MAX = 1472;
static constexpr uint32_t REALTIME_TIMEOUT_MS = 1500;

static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

static char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int asciiCaseCompare(const char* a, const char* b) {
    while (*a && *b) {
        char ca = asciiLower(*a);
        char cb = asciiLower(*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int ipCompare(const IPAddress& a, const IPAddress& b) {
    for (uint8_t i = 0; i < 4; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

static String compactMac() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    return mac;
}

static uint16_t softApSuffix() {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        return ((uint16_t)mac[4] << 8) | mac[5];
    }
    uint64_t efuse = ESP.getEfuseMac();
    uint32_t folded = (uint32_t)efuse ^ (uint32_t)(efuse >> 16) ^ (uint32_t)(efuse >> 32);
    return (uint16_t)(folded ^ (folded >> 16));
}

static String fallbackApSsid(const AppConfig& cfg) {
    (void)cfg;
    char apSsid[32];
    snprintf(apSsid, sizeof(apSsid), "AuraX-%04X", softApSuffix());
    return String(apSsid);
}

static constexpr uint32_t FW_AUTO_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t FW_HTTP_TIMEOUT_MS = 3500;

static const esp_partition_t* findPartition(esp_partition_type_t type, esp_partition_subtype_t subtype, const char* label) {
    return esp_partition_find_first(type, subtype, label);
}

static String partitionLayoutName() {
    const esp_partition_t* app0 = findPartition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t* app1 = findPartition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t* fs = findPartition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");

    if (app0 && app1 && fs) {
        if (app0->size == 0x200000 && app1->size == 0x200000 && fs->address == 0x410000) {
            return "aurax_wled_8mb_2m_ota";
        }
        if (app0->size == 0x180000 && app1->size == 0x180000 && fs->address == 0x310000) {
            return "aurax_8mb_1m5_ota";
        }
    }
    return "custom";
}

static uint8_t wifiSignalPct(int32_t rssi) {
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return (uint8_t)((rssi + 100) * 2);
}

static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "UNKNOWN";
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "OTHER";
    }
}

static void applyWifiStabilitySettings() {
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

static uint8_t clampWifiChannel(int32_t channel) {
    return (channel >= 1 && channel <= 13) ? (uint8_t)channel : 1;
}

static uint8_t scanSavedSsidChannel(const char* ssid) {
    if (!ssid || !ssid[0]) return 1;

    int bestRssi = -1000;
    uint8_t bestChannel = 1;
    int count = WiFi.scanNetworks(false, true, false, 220);
    LOG("[wifi] channel scan found %d networks\n", count);

    for (int i = 0; i < count; i++) {
        if (WiFi.SSID(i) != ssid) continue;
        int rssi = WiFi.RSSI(i);
        uint8_t channel = clampWifiChannel(WiFi.channel(i));
        LOG("[wifi] saved ssid %s seen on ch=%u rssi=%d auth=%d\n",
            ssid, channel, rssi, WiFi.encryptionType(i));
        if (rssi > bestRssi) {
            bestRssi = rssi;
            bestChannel = channel;
        }
    }

    WiFi.scanDelete();
    return bestChannel;
}

static void logWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            LOG("[wifi] event: STA disconnected reason=%u\n", info.wifi_sta_disconnected.reason);
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOGLN("[wifi] event: STA connected");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG("[wifi] event: STA got IP %s\n", WiFi.localIP().toString().c_str());
            break;
        default:
            break;
    }
}

static void ensureWifiEventLogging() {
    static bool registered = false;
    if (registered) return;
    WiFi.onEvent(logWifiEvent);
    registered = true;
}

static uint8_t auraBrightnessToWled(uint8_t pct) {
    if (pct == 0) return 255;  // AuraX 0 = use per-pixel program brightness.
    if (pct > 100) pct = 100;
    uint16_t bri = ((uint16_t)pct * 255u + 50u) / 100u;
    return (uint8_t)(bri < 1 ? 1 : bri);
}

static uint8_t wledBrightnessToAura(uint16_t bri) {
    if (bri > 255) bri = 255;
    uint16_t pct = (bri * 100u + 127u) / 255u;
    return (uint8_t)(pct < 1 && bri > 0 ? 1 : pct);
}

static bool isProgramExtension(const String& lowerName) {
    return lowerName.endsWith(".pix") || lowerName.endsWith(".axp") || lowerName.endsWith(".apx");
}

static String playerLoadErrorText(int err) {
    if (err == 7) {
        return "Spatne nastaveni PX: program neodpovida aktualnimu poctu pixelu v zarizeni.";
    }
    return "load failed: " + String(err);
}

static String programPxMismatchText(uint32_t programPx, uint16_t devicePx) {
    return "Spatne nastaveni PX: program ma " + String(programPx) +
           " px, ale zarizeni je nastavene na " + String(devicePx) +
           " px. U CONTACT POI nahraj program pro logicky pocet PX.";
}

static bool readProgramDw(File& f, uint32_t& out) {
    uint8_t b[4];
    if (f.read(b, 4) != 4) return false;
    out = (uint32_t)b[0]
        | ((uint32_t)b[1] << 8)
        | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
    return true;
}

static void splitProgramDw(uint32_t dw, uint8_t& label, uint8_t& type, uint16_t& size) {
    label = (dw >> 24) & 0xFF;
    type  = (dw >> 16) & 0xFF;
    size  = dw & 0xFFFF;
}

static bool programMatchesLedCount(const String& path, uint16_t ledCount, String& error) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        error = "Program validation failed: cannot open file";
        return false;
    }

    uint32_t dw = 0;
    if (!readProgramDw(f, dw)) {
        f.close();
        error = "Program validation failed: file is too small";
        return false;
    }

    if (dw == 0x31505841) {  // "AXP1"
        uint32_t version = 0, commandCount = 0, numLeds = 0, endBehavior = 0, decodedBytes = 0;
        bool ok = readProgramDw(f, version) && readProgramDw(f, commandCount) &&
                  readProgramDw(f, numLeds) && readProgramDw(f, endBehavior) &&
                  readProgramDw(f, decodedBytes);
        (void)endBehavior;
        (void)decodedBytes;
        if (!ok || commandCount == 0 || commandCount > 64 || version < 1 || version > 2) {
            f.close();
            error = "Program validation failed: invalid AXP header";
            return false;
        }
        if (numLeds != 0 && numLeds != ledCount) {
            f.close();
            error = programPxMismatchText(numLeds, ledCount);
            return false;
        }
        for (uint32_t i = 0; i < commandCount; i++) {
            uint32_t startTime = 0, endTime = 0, width = 0, height = 0, frequency = 0;
            uint32_t dataOffset = 0, dataSize = 0, decodedOffset = 0, codec = 0, isLast = 0;
            ok = readProgramDw(f, startTime) && readProgramDw(f, endTime) &&
                 readProgramDw(f, width) && readProgramDw(f, height) &&
                 readProgramDw(f, frequency) && readProgramDw(f, dataOffset) &&
                 readProgramDw(f, dataSize) && readProgramDw(f, decodedOffset) &&
                 readProgramDw(f, codec) && readProgramDw(f, isLast);
            (void)startTime; (void)endTime; (void)height; (void)frequency;
            (void)dataOffset; (void)dataSize; (void)decodedOffset; (void)codec; (void)isLast;
            if (!ok) {
                f.close();
                error = "Program validation failed: incomplete AXP command table";
                return false;
            }
            if (width != ledCount) {
                f.close();
                error = programPxMismatchText(width, ledCount);
                return false;
            }
        }
        f.close();
        return true;
    }

    f.seek(0);
    while (true) {
        if (!readProgramDw(f, dw)) break;
        uint8_t label, type;
        uint16_t size;
        splitProgramDw(dw, label, type, size);
        if (label != 0xD1) {
            f.seek(f.position() - 4);
            break;
        }
        f.seek(f.position() + (uint32_t)size * 4u);
    }

    while (readProgramDw(f, dw)) {
        uint8_t label, type;
        uint16_t size;
        splitProgramDw(dw, label, type, size);
        if (label != 0xA1) break;
        if (type != 1) {
            if (size > 0) f.seek(f.position() + (uint32_t)(size - 1) * 4u);
            continue;
        }

        int remaining = (int)size - 1;
        while (remaining > 0 && readProgramDw(f, dw)) {
            remaining--;
            uint8_t pl, pt;
            uint16_t ps;
            splitProgramDw(dw, pl, pt, ps);
            if (pl != 0xB1) {
                f.seek(f.position() - 4);
                break;
            }
            uint32_t val = 0;
            if (ps >= 1) {
                if (!readProgramDw(f, val)) {
                    f.close();
                    error = "Program validation failed: incomplete PIX parameter";
                    return false;
                }
                remaining--;
            }
            if (ps > 1) {
                f.seek(f.position() + (uint32_t)(ps - 1) * 4u);
                remaining -= (int)ps - 1;
            }
            if (pt == 0x0A && val != ledCount) {
                f.close();
                error = programPxMismatchText(val, ledCount);
                return false;
            }
        }
    }

    f.close();
    return true;
}

static String sanitizeProgramPath(const String& input) {
    String name = input;
    name.replace("\\", "/");
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    name.trim();
    String clean;
    for (size_t i = 0; i < name.length() && clean.length() < 58; i++) {
        char c = name.charAt(i);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            clean += c;
        }
    }
    if (clean.length() == 0) clean = "show.pix";
    String lower = clean;
    lower.toLowerCase();
    if (!isProgramExtension(lower)) clean += ".pix";
    return "/" + clean;
}

static size_t fileSizeOf(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    size_t size = f.size();
    f.close();
    return size;
}

struct ProgramEntry {
    String path;
    String displayName;
    size_t size;
    uint16_t storedSlot;
};

static uint16_t storedSlotFromPath(const String& path) {
    int start = path.startsWith("/") ? 1 : 0;
    if (path.length() < start + 4) return 0;
    char a = path.charAt(start);
    char b = path.charAt(start + 1);
    char c = path.charAt(start + 2);
    char sep = path.charAt(start + 3);
    if (a < '0' || a > '9' || b < '0' || b > '9' || c < '0' || c > '9') return 0;
    if (sep != '-' && sep != '_') return 0;
    return (uint16_t)((a - '0') * 100 + (b - '0') * 10 + (c - '0'));
}

static String programDisplayName(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (storedSlotFromPath(path) != 0 && name.length() > 4) name = name.substring(4);
    return name.length() ? name : "program.pix";
}

static String numberedProgramPath(uint16_t slot, const String& displayName) {
    String cleanPath = sanitizeProgramPath(displayName);
    String clean = cleanPath.startsWith("/") ? cleanPath.substring(1) : cleanPath;
    if (storedSlotFromPath("/" + clean) != 0 && clean.length() > 4) clean = clean.substring(4);
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%03u-", (unsigned)slot);
    return "/" + String(prefix) + clean;
}

static int collectPrograms(ProgramEntry* entries, int maxEntries) {
    int count = 0;
    File root = LittleFS.open("/");
    if (!root) return 0;
    File file = root.openNextFile();
    while (file && count < maxEntries) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (!name.startsWith("/")) name = "/" + name;
            String lower = name;
            lower.toLowerCase();
            if (isProgramExtension(lower)) {
                entries[count].path = name;
                entries[count].displayName = programDisplayName(name);
                entries[count].size = file.size();
                entries[count].storedSlot = storedSlotFromPath(name);
                count++;
            }
        }
        file = root.openNextFile();
    }
    root.close();

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            bool swap = false;
            if (entries[i].storedSlot && entries[j].storedSlot) {
                swap = entries[j].storedSlot < entries[i].storedSlot;
            } else if (entries[j].storedSlot && !entries[i].storedSlot) {
                swap = true;
            } else if (entries[i].storedSlot == entries[j].storedSlot) {
                swap = entries[j].path.compareTo(entries[i].path) < 0;
            }
            if (swap) {
                ProgramEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    return count;
}

static bool applyProgramOrder(ProgramEntry* entries, int count, AppConfig* cfg) {
    String selected = cfg ? String(cfg->pixFile) : "";
    String tempPaths[32];
    int selectedIndex = -1;

    for (int i = 0; i < count; i++) {
        if (entries[i].path == selected) selectedIndex = i;
        tempPaths[i] = "/__aurax_tmp_" + String(i);
        if (LittleFS.exists(tempPaths[i])) LittleFS.remove(tempPaths[i]);
        if (!LittleFS.rename(entries[i].path, tempPaths[i])) return false;
    }

    for (int i = 0; i < count; i++) {
        String finalPath = numberedProgramPath((uint16_t)(i + 1), entries[i].displayName);
        if (LittleFS.exists(finalPath)) LittleFS.remove(finalPath);
        if (!LittleFS.rename(tempPaths[i], finalPath)) return false;
        if (cfg && i == selectedIndex) strlcpy(cfg->pixFile, finalPath.c_str(), sizeof(cfg->pixFile));
    }
    return true;
}

static bool renumberPrograms(AppConfig* cfg) {
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    if (count == 0) return true;
    return applyProgramOrder(entries, count, cfg);
}

static bool programPathForSlot(uint16_t slot, String& out) {
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    if (slot < 1 || slot > count) return false;
    out = entries[slot - 1].path;
    return true;
}

static uint16_t slotForProgramPath(const String& path) {
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    for (int i = 0; i < count; i++) {
        if (entries[i].path == path) return (uint16_t)(i + 1);
    }
    return 0;
}

static String nextUploadProgramPath(const String& filename) {
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    return numberedProgramPath((uint16_t)(count + 1), programDisplayName(sanitizeProgramPath(filename)));
}

static uint8_t firstSyncChannel(uint16_t mask) {
    for (uint8_t ch = 1; ch <= 10; ch++) {
        if (mask & (1u << (ch - 1))) return ch;
    }
    return 0;
}

static EffectParams effectParamsFromConfig(const AppConfig& cfg) {
    EffectParams p = {};
    p.effectId    = cfg.effectId;
    p.speed       = cfg.effectSpeed;
    p.intensity   = cfg.effectIntensity;
    p.dotSize     = cfg.effectDotSize;
    p.paletteId   = cfg.effectPaletteId;
    p.paletteSize = cfg.paletteSize;
    p.reverse     = cfg.effectReverse;
    if (p.paletteSize == 0 || p.paletteSize > 4) p.paletteSize = 1;
    for (int i = 0; i < p.paletteSize && i < 4; i++) {
        p.palette[i] = {cfg.paletteR[i], cfg.paletteG[i], cfg.paletteB[i]};
    }
    return p;
}

static uint16_t normalizeEffectSpeedValue(int speed) {
    if (speed < 0) return 0;
    if (speed > 255) {
        speed = speed > 1000 ? 255 : (speed * 255 + 500) / 1000;
    }
    return speed > 255 ? 255 : (uint16_t)speed;
}

// ── WifiControl ───────────────────────────────────────────────────────────────

void WifiControl::mdnsBegin(const char* hostname) {
    const char* mdnsHost = strlen(hostname) ? hostname : "aurax";
    if (MDNS.begin(mdnsHost)) {
        String instanceName = "AuraX " + String(mdnsHost);
        MDNS.setInstanceName(instanceName.c_str());
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("aurax", "tcp", 80);
        MDNS.addServiceTxt("aurax", "tcp", "hostname", mdnsHost);
        MDNS.addServiceTxt("aurax", "tcp", "ip", activeIP().toString().c_str());
        String mac = compactMac();
        MDNS.addService("wled", "tcp", 80);
        MDNS.addServiceTxt("wled", "tcp", "mac", mac.c_str());
        MDNS.addServiceTxt("wled", "tcp", "brand", "AuraX");
        MDNS.addServiceTxt("wled", "tcp", "type", "wled");
        MDNS.addServiceTxt("wled", "tcp", "name", mdnsHost);
        MDNS.addService("ddp", "udp", DDP_REALTIME_PORT);
        MDNS.addServiceTxt("ddp", "udp", "name", mdnsHost);
        LOG("[mdns] http://%s.local\n", mdnsHost);
    }
    if (strcmp(mdnsHost, "aurax") != 0) {
        mdns_ip_addr_t addr = {};
        addr.addr.type = ESP_IPADDR_TYPE_V4;
        addr.addr.u_addr.ip4.addr = (_apMode ? WiFi.softAPIP() : WiFi.localIP());
        addr.next = nullptr;
        if (mdns_delegate_hostname_add("aurax", &addr) == ESP_OK)
            LOGLN("[mdns] alias: aurax.local → " + String(mdnsHost) + ".local");
    }
}

WifiControl::WifiControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds, AppConfig& cfg,
                         SyncControl* sync, bool fsMounted)
    : _player(player), _effectPlayer(effectPlayer), _leds(leds), _cfg(cfg), _sync(sync), _fsMounted(fsMounted) {
    if (_sync) {
        _sync->setRescueHandler(
            [](uint8_t action, void* ctx) {
                if (ctx) static_cast<WifiControl*>(ctx)->rescueAction(action);
            },
            this);
        _sync->setStateHandlers(
            [](const char* file, uint8_t endBehavior, void* ctx) {
                if (ctx) static_cast<WifiControl*>(ctx)->rememberSyncedProgram(file, endBehavior);
            },
            [](const EffectParams& p, void* ctx) {
                if (ctx) static_cast<WifiControl*>(ctx)->rememberSyncedEffect(p);
            },
            [](uint8_t brightness, void* ctx) {
                if (ctx) static_cast<WifiControl*>(ctx)->rememberSyncedBrightness(brightness);
            },
            this);
    }
}

bool WifiControl::saveRuntimeConfig() {
    if (!_fsMounted) {
        _fsMounted = LittleFS.begin(false);
        if (!_fsMounted) _fsMounted = LittleFS.begin(true);
    }
    if (!_fsMounted) return false;
    AppConfig saved = _cfg;
    if (strlen(_wantedHostname) > 0)
        strlcpy(saved.hostname, _wantedHostname, sizeof(saved.hostname));
    return saveConfig(saved);
}

void WifiControl::scheduleRuntimeConfigSave(uint32_t delayMs) {
    _runtimeSavePending = true;
    _runtimeSaveAtMs = millis() + delayMs;
}

void WifiControl::flushRuntimeConfigSave() {
    if (!_runtimeSavePending) return;
    int32_t remaining = (int32_t)(millis() - _runtimeSaveAtMs);
    if (remaining < 0) return;
    _runtimeSavePending = false;
    saveRuntimeConfig();
}

void WifiControl::rememberSyncedProgram(const char* file, uint8_t endBehavior) {
    if (!file || !file[0]) return;
    strlcpy(_cfg.pixFile, file, sizeof(_cfg.pixFile));
    _cfg.endBehavior = endBehavior;
    _cfg.autoStart = 0;
    saveRuntimeConfig();
}

void WifiControl::rememberSyncedEffect(const EffectParams& p) {
    _cfg.autoStart = 1;
    _cfg.effectId = p.effectId;
    _cfg.effectSpeed = p.speed;
    _cfg.effectIntensity = p.intensity;
    _cfg.effectDotSize = p.dotSize;
    _cfg.effectPaletteId = p.paletteId;
    _cfg.effectReverse = p.reverse ? 1 : 0;
    _cfg.paletteSize = p.paletteSize ? p.paletteSize : 1;
    if (_cfg.paletteSize > 4) _cfg.paletteSize = 4;
    for (int i = 0; i < _cfg.paletteSize; i++) {
        _cfg.paletteR[i] = p.palette[i].r;
        _cfg.paletteG[i] = p.palette[i].g;
        _cfg.paletteB[i] = p.palette[i].b;
    }
    scheduleRuntimeConfigSave();
}

void WifiControl::rememberSyncedBrightness(uint8_t brightness) {
    _cfg.brightness = brightness > 100 ? 100 : brightness;
    scheduleRuntimeConfigSave();
}

void WifiControl::beginRealtimeUdp() {
    if (_realtimeUdpStarted) return;
    _wledRealtimeUdp.begin(WLED_REALTIME_PORT);
    _ddpUdp.begin(DDP_REALTIME_PORT);
    _realtimeUdpStarted = true;
    LOG("[realtime] WLED UDP %u, DDP %u ready\n", WLED_REALTIME_PORT, DDP_REALTIME_PORT);
}

bool WifiControl::ensureRealtimeBuffer() {
    uint16_t count = _leds.logicalNumLeds();
    if (count == 0) return false;
    if (_realtimeBuf && _realtimeBufLeds == count) return true;
    if (_realtimeBuf) {
        free(_realtimeBuf);
        _realtimeBuf = nullptr;
        _realtimeBufLeds = 0;
    }
    _realtimeBuf = (uint8_t*)malloc((size_t)count * 4);
    if (!_realtimeBuf) {
        LOGLN("[realtime] buffer allocation failed");
        return false;
    }
    _realtimeBufLeds = count;
    memset(_realtimeBuf, 0, (size_t)count * 4);
    for (uint16_t i = 0; i < count; i++) _realtimeBuf[(size_t)i * 4] = 0xFF;
    return true;
}

void WifiControl::enterRealtimeMode() {
    _lastRealtimeMs = millis();
    if (_realtimeActive) return;
    _realtimeActive = true;
    _effectPlayer.stop();
    _player.stopTask();
    _player.unload();
    if (ensureRealtimeBuffer()) {
        memset(_realtimeBuf, 0, (size_t)_realtimeBufLeds * 4);
        for (uint16_t i = 0; i < _realtimeBufLeds; i++) _realtimeBuf[(size_t)i * 4] = 0xFF;
    }
}

void WifiControl::writeRealtimeRgb(uint32_t byteOffset, const uint8_t* rgb, uint16_t len) {
    if (!rgb || len == 0 || !ensureRealtimeBuffer()) return;
    for (uint16_t i = 0; i < len; i++) {
        uint32_t absolute = byteOffset + i;
        uint16_t led = (uint16_t)(absolute / 3);
        if (led >= _realtimeBufLeds) continue;
        uint8_t component = absolute % 3;
        uint8_t* dst = _realtimeBuf + (size_t)led * 4;
        dst[0] = 0xFF;
        if (component == 0) dst[3] = rgb[i];      // R
        else if (component == 1) dst[2] = rgb[i]; // G
        else dst[1] = rgb[i];                     // B
    }
}

void WifiControl::showRealtimeBuffer() {
    if (!_realtimeBuf || _realtimeBufLeds == 0) return;
    _leds.showColumnDirect(_realtimeBuf, _realtimeBufLeds);
}

void WifiControl::handleDdpPacket(uint8_t* packet, int len) {
    if (!packet || len < 10) return;
    uint16_t payloadLen = ((uint16_t)packet[8] << 8) | packet[9];
    if (payloadLen == 0) return;
    if (payloadLen > (uint16_t)(len - 10)) payloadLen = (uint16_t)(len - 10);
    uint32_t offset = ((uint32_t)packet[4] << 24) | ((uint32_t)packet[5] << 16) |
                      ((uint32_t)packet[6] << 8) | packet[7];
    enterRealtimeMode();
    writeRealtimeRgb(offset, packet + 10, payloadLen);
    showRealtimeBuffer();
}

void WifiControl::handleWledRealtimePacket(uint8_t* packet, int len) {
    if (!packet || len < 2) return;
    uint8_t protocol = packet[0];
    enterRealtimeMode();
    if (protocol == 1) {  // WARLS: timeout, then index/R/G/B tuples.
        for (int i = 2; i + 3 < len; i += 4) {
            uint32_t offset = (uint32_t)packet[i] * 3u;
            writeRealtimeRgb(offset, packet + i + 1, 3);
        }
    } else if (protocol == 2) {  // DRGB: timeout, then RGB stream from LED 0.
        writeRealtimeRgb(0, packet + 2, (uint16_t)(len - 2));
    } else if (protocol == 3) {  // DRGBW: ignore W channel.
        uint32_t led = 0;
        for (int i = 2; i + 3 < len; i += 4, led++) {
            uint8_t rgb[3] = {packet[i], packet[i + 1], packet[i + 2]};
            writeRealtimeRgb(led * 3u, rgb, 3);
        }
    } else if (protocol == 4 && len >= 4) {  // DNRGB: timeout, start LED, RGB stream.
        uint16_t startLed = ((uint16_t)packet[2] << 8) | packet[3];
        writeRealtimeRgb((uint32_t)startLed * 3u, packet + 4, (uint16_t)(len - 4));
    }
    showRealtimeBuffer();
}

void WifiControl::receiveRealtimeUdp() {
    if (!_realtimeUdpStarted) return;
    static uint8_t packet[REALTIME_PACKET_MAX];
    int len = _ddpUdp.parsePacket();
    while (len > 0) {
        int readLen = _ddpUdp.read(packet, len > REALTIME_PACKET_MAX ? REALTIME_PACKET_MAX : len);
        handleDdpPacket(packet, readLen);
        len = _ddpUdp.parsePacket();
    }
    len = _wledRealtimeUdp.parsePacket();
    while (len > 0) {
        int readLen = _wledRealtimeUdp.read(packet, len > REALTIME_PACKET_MAX ? REALTIME_PACKET_MAX : len);
        handleWledRealtimePacket(packet, readLen);
        len = _wledRealtimeUdp.parsePacket();
    }
    if (_realtimeActive && millis() - _lastRealtimeMs > REALTIME_TIMEOUT_MS) {
        _realtimeActive = false;
    }
}

bool WifiControl::storageReady() {
    return _fsMounted;
}

IPAddress WifiControl::activeIP() const {
    return _apMode ? WiFi.softAPIP() : WiFi.localIP();
}

String WifiControl::rootUrl() const {
    return "http://" + activeIP().toString() + "/";
}

bool WifiControl::isIpHost(const String& host) const {
    if (host.length() == 0) return false;
    for (size_t i = 0; i < host.length(); i++) {
        char c = host.charAt(i);
        if (c == ':') break;  // strip optional :port
        if (c != '.' && (c < '0' || c > '9')) return false;
    }
    return true;
}

bool WifiControl::shouldRedirectCaptive() {
    if (!_apActive) return false;
    String host = _server.hostHeader();
    if (host.length() == 0) return false;
    host.toLowerCase();
    int portSep = host.indexOf(':');
    if (portSep >= 0) host = host.substring(0, portSep);
    if (isIpHost(host)) return false;
    if (host == "aurax.local" || host == "aurax") return false;
    if (host == String(_cfg.hostname) + ".local" || host == String(_cfg.hostname)) return false;
    return true;
}

uint8_t WifiControl::apClientCount() const {
    if (!_apActive) return 0;
    wifi_sta_list_t stationList;
    if (esp_wifi_ap_get_sta_list(&stationList) != ESP_OK) return 0;
    return stationList.num;
}

const char* WifiControl::staSsid() const {
    return _cfg.ssid;
}

const char* WifiControl::staPassword() const {
    return _cfg.password;
}

bool WifiControl::connectSta(uint32_t timeoutMs) {
    _dns.stop();
    _apActive = false;
    _apMode = false;
    _apHadClient = false;
    _staServicesStarted = false;

    WiFi.disconnect(false, false);
    WiFi.setHostname(_cfg.hostname);
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    applyWifiStabilitySettings();
    delay(100);
    _fallbackApChannel = scanSavedSsidChannel(staSsid());

    WiFi.begin(staSsid(), staPassword());
    applyWifiStabilitySettings();
    WiFi.setHostname(_cfg.hostname);
    LOG("[wifi] connecting to %s passLen=%u", staSsid(), (unsigned)strlen(staPassword()));

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
        LOG("%c", '.');
    }

    _lastStaRetryMs = millis();
    _staDisconnectedSinceMs = _lastStaRetryMs;

    if (WiFi.status() != WL_CONNECTED) {
        LOGLN("\n[wifi] connect timeout");
        return false;
    }

    _staDisconnectedSinceMs = 0;
    _lastStaRetryMs = 0;
    LOG("\n[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool WifiControl::startSoftApRadio() {
    String apSsid = fallbackApSsid(_cfg);
    IPAddress apIP(192, 168, 4, 1);
    uint8_t apChannel = clampWifiChannel(_fallbackApChannel);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAPsetHostname(_cfg.hostname);
    bool apOk = WiFi.softAP(apSsid.c_str(), nullptr, apChannel);
    LOG("[wifi] AP radio: SSID=%s IP=%s ch=%u ok=%u\n",
        apSsid.c_str(), WiFi.softAPIP().toString().c_str(), apChannel, apOk ? 1 : 0);
    return apOk;
}

void WifiControl::startFallbackAp() {
    if (_apActive) return;

    _apMode = true;
    _apHadClient = false;
    WiFi.scanDelete();
    WiFi.setHostname(_cfg.hostname);
    WiFi.mode(WIFI_AP);
    applyWifiStabilitySettings();

    bool apOk = startSoftApRadio();
    _apActive = true;
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", WiFi.softAPIP());
    MDNS.end();
    mdnsBegin(_cfg.hostname);
    NBNS.begin(_cfg.hostname);
    beginRealtimeUdp();
    LOG("[wifi] AP fallback active ok=%u\n", apOk ? 1 : 0);

    if (strlen(staSsid())) {
        LOGLN("[wifi] AP is stable; STA retry disabled until config save or reboot");
    }
}

void WifiControl::stopFallbackAp() {
    if (!_apActive) return;
    _dns.stop();
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    _apActive = false;
    _apMode = false;
    WiFi.setHostname(_cfg.hostname);
    WiFi.mode(WIFI_STA);
    applyWifiStabilitySettings();
    LOG("[wifi] AP disabled, STA IP: %s\n", WiFi.localIP().toString().c_str());
}

void WifiControl::rescueAction(uint8_t action) {
    if (action == SyncControl::RESCUE_REBOOT) {
        LOGLN("[rescue] reboot requested");
        delay(100);
        esp_restart();
        return;
    }

    if (action == SyncControl::RESCUE_FORCE_AP) {
        LOGLN("[rescue] force AP requested");
        startFallbackAp();
        return;
    }

    if (action == SyncControl::RESCUE_STA_RETRY) {
        LOGLN("[rescue] STA retry requested");
        if (strlen(staSsid()) == 0) {
            startFallbackAp();
            return;
        }
        stopFallbackAp();
        connectSta(STA_CONNECT_TIMEOUT_MS);
        if (WiFi.status() == WL_CONNECTED) {
            startStaServices();
        } else {
            startFallbackAp();
        }
    }
}

void WifiControl::startStaServices() {
    if (_staServicesStarted || WiFi.status() != WL_CONNECTED) return;
    MDNS.end();
    mdnsBegin(_cfg.hostname);
    NBNS.begin(_cfg.hostname);
    _udp.begin(DISCOVERY_PORT);
    beginRealtimeUdp();
    announce();
    ArduinoOTA.setHostname(_cfg.hostname);
    ArduinoOTA.begin();
    _staServicesStarted = true;
    LOG("[ota] ArduinoOTA ready\n");
}

bool WifiControl::begin(uint32_t timeoutMs) {
    ensureWifiEventLogging();
    WiFi.persistent(false);
    WiFi.softAPdisconnect(true);

    if (strlen(staSsid()) == 0) {
        LOGLN("[wifi] no STA config, starting AP");
        startFallbackAp();
    } else if (!connectSta(timeoutMs)) {
        startFallbackAp();
    }
    _server.on("/",            HTTP_GET, [this]() { handleRoot(); });
    _server.on("/experimental", HTTP_GET, [this]() {
        _server.send_P(200, "text/html", EXPERIMENTAL_HTML);
    });
    _server.on("/play",   HTTP_GET,  [this]() { handlePlay();      });
    _server.on("/stop",   HTTP_GET,  [this]() { handleStop();      });
    _server.on("/off",    HTTP_GET,  [this]() {
        bool relay = requestAllowsRelay();
        if (_sync && relay) {
            _sync->broadcastStop();
            fanoutStop();
        } else {
            _effectPlayer.stop();
            _player.blackout();
        }
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/power", HTTP_POST, [this]() { handlePower(); });
    _server.on("/status", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/status", HTTP_GET,     [this]() { handleStatus();      });
    _server.on("/json", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/json", HTTP_GET, [this]() { handleWledJson(); });
    _server.on("/json/si", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/json/si", HTTP_GET, [this]() { handleWledJson(); });
    _server.on("/json/info", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/json/info", HTTP_GET, [this]() { handleWledInfo(); });
    _server.on("/json/state", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/json/state", HTTP_GET, [this]() { handleWledState(); });
    _server.on("/json/state", HTTP_POST, [this]() { handleWledStatePost(); });
    _server.on("/json/effects", HTTP_GET, [this]() { handleWledEffects(); });
    _server.on("/json/palettes", HTTP_GET, [this]() { handleWledPalettes(); });
    _server.on("/config", HTTP_GET,  [this]() { handleConfigGet(); });
    _server.on("/config", HTTP_POST, [this]() { handleConfigPost(); });
    _server.on("/fw/status", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/fw/status", HTTP_GET, [this]() { handleFirmwareStatus(); });
    _server.on("/fw/check", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/fw/check", HTTP_POST, [this]() { handleFirmwareCheck(); });
    _server.on("/rescue", HTTP_POST, [this]() { handleRescue(); });
    _server.on("/programs", HTTP_GET, [this]() { handlePrograms(); });
    _server.on("/program/download", HTTP_GET, [this]() { handleProgramDownload(); });
    _server.on("/program/select", HTTP_POST, [this]() { handleProgramSelect(); });
    _server.on("/program/delete", HTTP_POST, [this]() { handleProgramDelete(); });
    _server.on("/program/reorder", HTTP_POST, [this]() { handleProgramReorder(); });
    _server.on("/program/start", HTTP_POST, [this]() { handleProgramStart(); });
    _server.on("/identify", HTTP_POST, [this]() { handleIdentify(); });
    _server.on("/sync", HTTP_POST, [this]() { handleSyncNow(); });
    _server.on("/reboot", HTTP_POST, [this]() {
        _server.send(200, "text/plain", "OK");
        delay(200);
        esp_restart();
    });
    _server.on("/storage/format", HTTP_POST, [this]() {
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
        _leds.clear();
        bool ok = LittleFS.format();
        if (ok) _fsMounted = LittleFS.begin(false);
        _server.send(ok ? 200 : 500, "text/plain",
            ok ? "Storage formatted, rebooting..." : "Storage format failed");
        delay(300);
        if (ok) esp_restart();
    });

    _server.on("/upload", HTTP_POST,
        [this]() {
            if (_uploadFile) _uploadFile.close();
            if (!_fsMounted) {
                _server.send(503, "text/plain", "Storage unavailable. Format storage first.");
                return;
            }
            if (!_uploadError) {
                String validationError;
                if (!programMatchesLedCount(String(_cfg.pixFile), _leds.logicalNumLeds(), validationError)) {
                    LittleFS.remove(_cfg.pixFile);
                    _server.send(400, "text/plain", validationError);
                    return;
                }
            }
            if (_uploadError) {
                if (_uploadPath.length()) LittleFS.remove(_uploadPath);
                _server.send(500, "text/plain", "Chyba: nedostatek místa v LittleFS");
            } else {
                _server.send(200, "text/plain", "OK — soubor nahrán jako " + String(_cfg.pixFile));
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
            if (!_fsMounted) {
                _uploadError = true;
                return;
            }
            if (up.status == UPLOAD_FILE_START) {
                _uploadError = false;
                _player.unload();
                if (LittleFS.exists(_cfg.pixFile)) LittleFS.remove(_cfg.pixFile);
                _uploadFile = LittleFS.open(_cfg.pixFile, "w");
                if (!_uploadFile) { LOGLN("[upload] open failed"); _uploadError = true; return; }
                LOG("[upload] start: %s\n", up.filename.c_str());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (_uploadFile && !_uploadError) {
                    if (_uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
                        LOGLN("[upload] write failed — disk full");
                        _uploadError = true;
                    }
                }
            } else if (up.status == UPLOAD_FILE_END) {
                if (_uploadFile) { _uploadFile.close(); LOG("[upload] done: %u bytes\n", up.totalSize); }
            }
        }
    );

    _server.on("/program/upload", HTTP_POST,
        [this]() {
            if (_uploadFile) _uploadFile.close();
            if (!_fsMounted) {
                _server.send(503, "text/plain", "Storage unavailable. Format storage first.");
                return;
            }
            if (_uploadError) {
                if (_uploadPath.length()) LittleFS.remove(_uploadPath);
                _server.send(413, "text/plain", "Upload failed: not enough LittleFS space");
            } else {
                String validationError;
                if (!programMatchesLedCount(_uploadPath, _leds.logicalNumLeds(), validationError)) {
                    if (_uploadPath.length()) LittleFS.remove(_uploadPath);
                    _server.send(400, "text/plain", validationError);
                    return;
                }
                strlcpy(_cfg.pixFile, _uploadPath.c_str(), sizeof(_cfg.pixFile));
                _cfg.autoStart = 0;
                renumberPrograms(&_cfg);
                saveRuntimeConfig();
                _server.send(200, "text/plain", "OK: uploaded " + _uploadPath);
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
            if (!_fsMounted) {
                _uploadError = true;
                return;
            }
            if (up.status == UPLOAD_FILE_START) {
                _uploadError = false;
                _uploadPath = nextUploadProgramPath(up.filename);
                _uploadWritten = 0;
                size_t existingSize = fileSizeOf(_uploadPath.c_str());
                size_t total = LittleFS.totalBytes();
                size_t used = LittleFS.usedBytes();
                _uploadMaxBytes = (total > used ? total - used : 0) + existingSize;
                _player.stopTask();
                _player.unload();
                if (LittleFS.exists(_uploadPath)) LittleFS.remove(_uploadPath);
                _uploadFile = LittleFS.open(_uploadPath, "w");
                if (!_uploadFile) { LOGLN("[program] upload open failed"); _uploadError = true; return; }
                LOG("[program] upload start: %s as %s free=%u\n", up.filename.c_str(), _uploadPath.c_str(), (unsigned)_uploadMaxBytes);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (_uploadFile && !_uploadError) {
                    if (_uploadWritten + up.currentSize > _uploadMaxBytes) {
                        LOGLN("[program] upload rejected: file too large");
                        _uploadError = true;
                        _uploadFile.close();
                        LittleFS.remove(_uploadPath);
                        return;
                    }
                    if (_uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
                        LOGLN("[program] upload write failed");
                        _uploadError = true;
                    } else {
                        _uploadWritten += up.currentSize;
                    }
                }
            } else if (up.status == UPLOAD_FILE_END) {
                if (_uploadFile) {
                    _uploadFile.close();
                    LOG("[program] upload done: %u bytes\n", up.totalSize);
                }
            }
        }
    );

    _server.on("/endbehavior", HTTP_GET, [this]() {
        int v = _server.arg("v").toInt();
        if (v < 0 || v > 3) v = 255;  // anything out of range → from file
        _cfg.endBehavior = (uint8_t)v;
        _player.setEndBehavior(_cfg.endBehavior);
        scheduleRuntimeConfigSave();
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/nudge", HTTP_GET, [this]() {
        int v = _server.arg("v").toInt();
        _player.nudge(v);
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/brightness", HTTP_GET, [this]() {
        int v = _server.arg("v").toInt();
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        _cfg.brightness = (uint8_t)v;
        if (_sync) {
            _sync->broadcastBrightness(_cfg.brightness);
        } else {
            _leds.setBrightness(_cfg.brightness);
            _player.setBrightness(_cfg.brightness);
        }
        scheduleRuntimeConfigSave();
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/effect",      HTTP_POST, [this]() { handleEffectStart(); });
    _server.on("/effect/stop", HTTP_GET,  [this]() { handleEffectStop();  });
    _server.on("/peers",  HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/peers",  HTTP_GET,     [this]() { handlePeers(); });
    _server.on("/generate_204",       HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/gen_204",            HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/library/test/success.html", HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/connecttest.txt",    HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.on("/ncsi.txt",           HTTP_GET, [this]() { handleCaptivePortal(); });
    _server.onNotFound([this]() {
        if (_apActive) {
            handleCaptivePortal();
        } else {
            _server.send(404, "text/plain", "Not found");
        }
    });
    _server.on("/update", HTTP_POST,
        [this]() {
            _server.send(Update.hasError() ? 500 : 200, "text/plain",
                         Update.hasError() ? "Chyba aktualizace" : "OK — rebooting");
            delay(500);
            esp_restart();
        },
        [this]() { handleOta(); }
    );

    _server.begin();

    _batMonitor.begin(_cfg);

    strlcpy(_wantedHostname, _cfg.hostname, sizeof(_wantedHostname));

    if (!_apMode) {
        startStaServices();
    }

    return true;
}

void WifiControl::maintainWifi() {
    if (strlen(staSsid()) == 0) return;

    uint32_t now = millis();

    if (_apActive) {
        uint8_t clients = apClientCount();
        if (clients > 0 && !_apHadClient) {
            _apHadClient = true;
            LOG("[wifi] AP client connected (%u)\n", clients);
        } else if (clients == 0 && _apHadClient) {
            _apHadClient = false;
            LOGLN("[wifi] AP client left");
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _staDisconnectedSinceMs = 0;
        startStaServices();
        return;
    }

    if (_staServicesStarted) {
        MDNS.end();
        _udp.stop();
        _staServicesStarted = false;
        _lastAnnounceMs = 0;
        LOGLN("[wifi] disconnected");
    }

    if (_staDisconnectedSinceMs == 0) _staDisconnectedSinceMs = now;
    if (now - _staDisconnectedSinceMs > STA_CONNECT_TIMEOUT_MS) {
        LOGLN("[wifi] reconnect timeout, starting stable AP fallback");
        startFallbackAp();
        return;
    }

    if (now - _lastStaRetryMs > STA_RETRY_INTERVAL_MS) {
        LOG("[wifi] reconnecting to %s\n", staSsid());
        WiFi.begin(staSsid(), staPassword());
        applyWifiStabilitySettings();
        _lastStaRetryMs = now;
    }
}

void WifiControl::handle() {
    _server.handleClient();
    if (_apActive) _dns.processNextRequest();
    receiveRealtimeUdp();
    maintainWifi();
    if (_sync) _sync->refreshWifiPeer();
    if (_batMonitor.update()) {
        _effectPlayer.stop();
        _player.blackout();
        LOG("[bat] auto-off: battery %u%% (<= %u%%)\n", _batMonitor.pct(), _cfg.batAutoOffThreshold);
    }
    if (_staServicesStarted) {
        ArduinoOTA.handle();
        receivePeers();
        expirePeers();
        if (millis() - _lastAnnounceMs > ANNOUNCE_INTERVAL_MS) announce();
    }
    flushRuntimeConfigSave();
}

// ── handlers ──────────────────────────────────────────────────────────────────

void WifiControl::handleRoot() {
    if (shouldRedirectCaptive()) {
        handleCaptivePortal();
        return;
    }
    _server.send_P(200, "text/html", CLIENT_HTML);
}

void WifiControl::handleCaptivePortal() {
    if (!_apActive || !shouldRedirectCaptive()) {
        _server.send_P(200, "text/html", CLIENT_HTML);
        return;
    }
    _server.sendHeader("Location", rootUrl());
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(302, "text/plain", "");
}

bool WifiControl::validateProgramForPlay(const String& path) {
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable. Format storage or flash LittleFS first.");
        return false;
    }
    if (!path.length() || !LittleFS.exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return false;
    }
    String validationError;
    if (!programMatchesLedCount(path, _leds.logicalNumLeds(), validationError)) {
        _server.send(409, "text/plain", validationError);
        return false;
    }
    return true;
}

bool WifiControl::requestAllowsRelay() {
    return _server.arg("relay") != "0";
}

bool WifiControl::shouldFanoutToPeer(const Peer& peer) const {
    if (!_cfg.syncEnabled || _cfg.syncMask == 0) return false;
    if (peer.ip == activeIP()) return false;
    if (millis() - peer.lastSeenMs > PEER_EXPIRE_MS) return false;
    if (peer.syncMask != 0 && (peer.syncMask & _cfg.syncMask) == 0) return false;
    if (peer.syncMask != 0 && !peer.syncEnabled) return false;
    return true;
}

void WifiControl::fanoutHttpGet(const char* pathAndQuery) {
#if defined(ARDUINO_ARCH_ESP32C3)
    (void)pathAndQuery;
    return;
#else
    if (!_staServicesStarted || !_cfg.syncEnabled || _cfg.syncMask == 0) return;
    sortPeers();
    for (int i = 0; i < _peerCount; i++) {
        if (!shouldFanoutToPeer(_peers[i])) continue;
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(180);
        http.setTimeout(180);
        String url = "http://" + _peers[i].ip.toString() + String(pathAndQuery);
        if (!http.begin(client, url)) continue;
        int code = http.GET();
        if (code <= 0) LOG("[sync-http] GET %s failed: %d\n", url.c_str(), code);
        http.end();
    }
#endif
}

void WifiControl::fanoutHttpPost(const char* path, const String& body) {
#if defined(ARDUINO_ARCH_ESP32C3)
    (void)path;
    (void)body;
    return;
#else
    if (!_staServicesStarted || !_cfg.syncEnabled || _cfg.syncMask == 0) return;
    sortPeers();
    for (int i = 0; i < _peerCount; i++) {
        if (!shouldFanoutToPeer(_peers[i])) continue;
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(180);
        http.setTimeout(180);
        String url = "http://" + _peers[i].ip.toString() + String(path);
        if (!http.begin(client, url)) continue;
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(body);
        if (code <= 0) LOG("[sync-http] POST %s failed: %d\n", url.c_str(), code);
        http.end();
    }
#endif
}

void WifiControl::fanoutStop() {
    fanoutHttpGet("/stop?relay=0");
}

void WifiControl::fanoutProgramStart(uint8_t slot, int64_t startUs) {
    if (slot == 0) return;
    int64_t ageUs = esp_timer_get_time() - startUs;
    if (ageUs < 0) ageUs = 0;
    uint32_t ageMs = (uint32_t)(ageUs / 1000);
    if (ageMs > 30000) ageMs = 30000;
    String body = "{\"slot\":" + String(slot) +
                  ",\"ageMs\":" + String(ageMs) +
                  ",\"relay\":false}";
    fanoutHttpPost("/program/start", body);
}

void WifiControl::fanoutEffect(const EffectParams& p) {
    String body;
    body.reserve(260);
    body += "{\"id\":" + String(p.effectId);
    body += ",\"speed\":" + String(p.speed);
    body += ",\"intensity\":" + String(p.intensity);
    body += ",\"dotSize\":" + String(p.dotSize);
    body += ",\"paletteId\":" + String(p.paletteId);
    body += ",\"persist\":false,\"relay\":false,\"colors\":[";
    uint8_t count = p.paletteSize > 4 ? 4 : p.paletteSize;
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) body += ",";
        body += "{\"r\":" + String(p.palette[i].r);
        body += ",\"g\":" + String(p.palette[i].g);
        body += ",\"b\":" + String(p.palette[i].b) + "}";
    }
    body += "]}";
    fanoutHttpPost("/effect", body);
}

void WifiControl::handlePlay() {
    int64_t requestUs = esp_timer_get_time();
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable. Format storage or flash LittleFS first.");
        return;
    }
    bool relay = requestAllowsRelay();
    if (!validateProgramForPlay(String(_cfg.pixFile))) return;
    uint8_t slot = (uint8_t)slotForProgramPath(_cfg.pixFile);
    _effectPlayer.stop();
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    if (_sync && relay) {
        uint32_t totalAgeMs = (uint32_t)((esp_timer_get_time() - requestUs) / 1000);
        if (totalAgeMs > 30000) totalAgeMs = 30000;
        int err = totalAgeMs > 0
            ? _sync->broadcastPlayFromAge(_cfg.pixFile, _cfg.endBehavior, totalAgeMs, (uint8_t)slotForProgramPath(_cfg.pixFile))
            : _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slotForProgramPath(_cfg.pixFile));
        if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
        fanoutProgramStart(slot, requestUs);
    } else {
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
        _player.scheduleStart(requestUs);
        _player.startTask();
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStop() {
    bool relay = requestAllowsRelay();
    if (_sync && relay) {
        _sync->broadcastStop();
        fanoutStop();
    } else {
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handlePrograms() {
    if (!_fsMounted) {
        String json = "{\"total\":0,\"used\":0,\"free\":0,\"selected\":\"\",\"selected_slot\":0,"
                      "\"storage_mounted\":false,\"error\":\"Storage unavailable\",\"files\":[]}";
        _server.send(200, "application/json", json);
        return;
    }
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    uint16_t selectedSlot = slotForProgramPath(_cfg.pixFile);
    String json = "{";
    json += "\"total\":" + String((unsigned)total) + ",";
    json += "\"used\":" + String((unsigned)used) + ",";
    json += "\"free\":" + String((unsigned)(total > used ? total - used : 0)) + ",";
    json += "\"selected\":\"" + jsonEscape(String(_cfg.pixFile)) + "\",";
    json += "\"selected_slot\":" + String(selectedSlot) + ",";
    json += "\"files\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += "{\"slot\":" + String(i + 1)
              + ",\"name\":\"" + jsonEscape(entries[i].path)
              + "\",\"display_name\":\"" + jsonEscape(entries[i].displayName)
              + "\",\"size\":" + String((unsigned)entries[i].size) + "}";
    }
    json += "]}";
    _server.send(200, "application/json", json);
}

void WifiControl::handleProgramDownload() {
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }

    String path;
    uint16_t slot = (uint16_t)_server.arg("slot").toInt();
    if (slot > 0) {
        if (!programPathForSlot(slot, path)) {
            _server.send(404, "text/plain", "Program slot not found");
            return;
        }
    } else {
        path = sanitizeProgramPath(_server.arg("file"));
    }

    if (!path.length() || !LittleFS.exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        _server.send(500, "text/plain", "Program open failed");
        return;
    }

    String filename = path;
    if (filename.startsWith("/")) filename.remove(0, 1);
    _server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    _server.streamFile(file, "application/octet-stream");
    file.close();
}

void WifiControl::handleProgramSelect() {
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    String path;
    uint16_t slot = doc["slot"] | 0;
    if (slot > 0) {
        if (!programPathForSlot(slot, path)) {
            _server.send(404, "text/plain", "Program slot not found");
            return;
        }
    } else {
        path = sanitizeProgramPath(doc["file"] | "");
    }
    if (!LittleFS.exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }
    strlcpy(_cfg.pixFile, path.c_str(), sizeof(_cfg.pixFile));
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramDelete() {
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    String path = sanitizeProgramPath(doc["file"] | "");
    if (!LittleFS.exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }
    if (path == String(_cfg.pixFile)) {
        _player.stopTask();
        _player.unload();
        strlcpy(_cfg.pixFile, "", sizeof(_cfg.pixFile));
        saveRuntimeConfig();
    }
    LittleFS.remove(path);
    renumberPrograms(&_cfg);
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramReorder() {
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    int slot = doc["slot"] | 0;
    int direction = doc["direction"] | 0;
    ProgramEntry entries[32];
    int count = collectPrograms(entries, 32);
    int idx = slot - 1;
    int target = idx + (direction < 0 ? -1 : 1);
    if (idx < 0 || idx >= count || target < 0 || target >= count || direction == 0) {
        _server.send(400, "text/plain", "Invalid order");
        return;
    }
    ProgramEntry tmp = entries[idx];
    entries[idx] = entries[target];
    entries[target] = tmp;
    if (!applyProgramOrder(entries, count, &_cfg)) {
        _server.send(500, "text/plain", "Reorder failed");
        return;
    }
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramStart() {
    int64_t requestUs = esp_timer_get_time();
    if (!_fsMounted) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    uint16_t slot = doc["slot"] | 0;
    uint32_t ageMs = doc["ageMs"] | 0;
    bool relay = doc["relay"] | true;
    if (ageMs > 30000) ageMs = 30000;
    String path;
    if (!programPathForSlot(slot, path)) {
        _server.send(404, "text/plain", "Program slot not found");
        return;
    }
    strlcpy(_cfg.pixFile, path.c_str(), sizeof(_cfg.pixFile));
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    if (!validateProgramForPlay(path)) return;
    int64_t startUs = requestUs - (int64_t)ageMs * 1000;
    if (_sync && relay) {
        uint32_t totalAgeMs = ageMs + (uint32_t)((esp_timer_get_time() - requestUs) / 1000);
        if (totalAgeMs > 30000) totalAgeMs = 30000;
        int err = totalAgeMs > 0
            ? _sync->broadcastPlayFromAge(_cfg.pixFile, _cfg.endBehavior, totalAgeMs, (uint8_t)slot)
            : _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slot);
        if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
        fanoutProgramStart((uint8_t)slot, startUs);
    } else {
        _effectPlayer.stop();
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
        _player.scheduleStart(startUs);
        _player.startTask();
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleIdentify() {
    _effectPlayer.stop();
    _player.stopTask();

    uint16_t count = _leds.logicalNumLeds();
    uint8_t* frame = (uint8_t*)malloc((size_t)count * 4);
    if (!frame) {
        _server.send(500, "text/plain", "Identify allocation failed");
        return;
    }
    for (uint16_t i = 0; i < count; i++) {
        frame[(size_t)i * 4 + 0] = 0xFF;  // full brightness
        frame[(size_t)i * 4 + 1] = 0;
        frame[(size_t)i * 4 + 2] = 0;
        frame[(size_t)i * 4 + 3] = 255;
    }

    for (int i = 0; i < 3; i++) {
        _leds.showColumnDirect(frame, count);
        delay(45);
        for (uint16_t j = 0; j < count; j++) {
            frame[(size_t)j * 4 + 0] = 0xE0;
            frame[(size_t)j * 4 + 1] = 0;
            frame[(size_t)j * 4 + 2] = 0;
            frame[(size_t)j * 4 + 3] = 0;
        }
        _leds.showColumnDirect(frame, count);
        delay(70);
        for (uint16_t j = 0; j < count; j++) {
            frame[(size_t)j * 4 + 0] = 0xFF;
            frame[(size_t)j * 4 + 1] = 0;
            frame[(size_t)j * 4 + 2] = 0;
            frame[(size_t)j * 4 + 3] = 255;
        }
    }
    free(frame);
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handlePower() {
    int64_t requestUs = esp_timer_get_time();
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    bool on = doc["on"] | false;
    uint32_t ageMs = doc["ageMs"] | 0;
    bool relay = doc["relay"] | true;
    if (ageMs > 30000) ageMs = 30000;
    if (!on) {
        if (_sync && relay) {
            _sync->broadcastStop();
            fanoutStop();
        } else {
            _effectPlayer.stop();
            _player.blackout();
        }
        _server.send(200, "text/plain", "OK");
        return;
    }

    if (_cfg.autoStart == 1) {
        EffectParams p = effectParamsFromConfig(_cfg);
        if (_sync && relay) {
            _sync->broadcastEffect(p);
            fanoutEffect(p);
        } else {
            _effectPlayer.start(p);
        }
    } else {
        uint16_t slot = slotForProgramPath(_cfg.pixFile);
        if (!validateProgramForPlay(String(_cfg.pixFile))) return;
        int64_t startUs = requestUs - (int64_t)ageMs * 1000;
        if (_sync && relay) {
            uint32_t totalAgeMs = ageMs + (uint32_t)((esp_timer_get_time() - requestUs) / 1000);
            if (totalAgeMs > 30000) totalAgeMs = 30000;
            int err = totalAgeMs > 0
                ? _sync->broadcastPlayFromAge(_cfg.pixFile, _cfg.endBehavior, totalAgeMs, (uint8_t)slot)
                : _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slot);
            if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
            fanoutProgramStart((uint8_t)slot, startUs);
        } else {
            _effectPlayer.stop();
            _player.stopTask();
            int err = _player.load(_cfg.pixFile);
            if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
            _player.scheduleStart(startUs);
            _player.startTask();
        }
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleSyncNow() {
    bool relay = requestAllowsRelay();
    if (_cfg.autoStart == 1) {
        EffectParams p = effectParamsFromConfig(_cfg);
        if (_sync && relay) {
            _sync->broadcastEffect(p);
            fanoutEffect(p);
        } else {
            _effectPlayer.start(p);
        }
    } else {
        if (!validateProgramForPlay(String(_cfg.pixFile))) return;
        uint8_t slot = (uint8_t)slotForProgramPath(_cfg.pixFile);
        int64_t startUs = esp_timer_get_time();
        if (_sync && relay) {
            int err = _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, slot);
            if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
            fanoutProgramStart(slot, startUs);
        } else {
            _effectPlayer.stop();
            _player.stopTask();
            int err = _player.load(_cfg.pixFile);
            if (err) { _server.send(500, "text/plain", playerLoadErrorText(err)); return; }
            _player.scheduleStart(startUs);
            _player.startTask();
        }
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStatus() {
    sendCorsHeaders();
    auto st = _player.stats();
    String apSsid;
    if (_apActive) {
        apSsid = fallbackApSsid(_cfg);
    }
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* fsPart = findPartition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    esp_reset_reason_t resetReason = esp_reset_reason();
    uint16_t effectiveMALimit = _cfg.mALimit;
    if (_cfg.ledType == LED_TYPE_APA102 && effectiveMALimit == 0) {
        effectiveMALimit = APA102_AUTO_CURRENT_LIMIT_MA;
    }
    String json = "{";
    json += "\"playing\":"          + String(_player.isLoaded() ? "true" : "false") + ",";
    json += "\"effect_running\":"   + String(_effectPlayer.isRunning() ? "true" : "false") + ",";
    json += "\"power_on\":"         + String((_player.isLoaded() || _effectPlayer.isRunning()) ? "true" : "false") + ",";
    json += "\"commands\":"         + String(_player.numCommands()) + ",";
    json += "\"file\":\""           + jsonEscape(String(_cfg.pixFile)) + "\",";
    json += "\"frames_rendered\":"  + String(st.framesRendered) + ",";
    json += "\"frames_expected\":"  + String(st.framesExpected) + ",";
    json += "\"fps_x10\":"          + String(_effectPlayer.isRunning() ? _effectPlayer.fpsX10() : st.fpsX10) + ",";
    json += "\"ip\":\""             + (_apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",";
    json += "\"hostname\":\""       + String(_cfg.hostname) + "\",";
    json += "\"device_name\":\""    + String(strlen(_wantedHostname) ? _wantedHostname : _cfg.hostname) + "\",";
    uint8_t wifiChannel = WiFi.channel();
    wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&wifiChannel, &secondChannel);
    json += "\"wifi_channel\":"     + String(wifiChannel) + ",";
    json += "\"ap_ssid\":\""        + jsonEscape(apSsid) + "\",";
    json += "\"ap_mode\":"          + String(_apMode ? "true" : "false") + ",";
    json += "\"storage_mounted\":"  + String(_fsMounted ? "true" : "false") + ",";
    json += "\"partition_layout\":\"" + partitionLayoutName() + "\",";
    json += "\"fw_version\":\""     + String(AURAX_FW_VERSION) + "\",";
    json += "\"fw_build\":"         + String(AURAX_FW_BUILD) + ",";
    json += "\"reset_reason\":"     + String((int)resetReason) + ",";
    json += "\"reset_reason_name\":\"" + String(resetReasonName(resetReason)) + "\",";
    json += "\"mA_limit\":"         + String(_cfg.mALimit) + ",";
    json += "\"effective_mA_limit\":" + String(effectiveMALimit) + ",";
    json += "\"led_count\":"        + String(_leds.physicalNumLeds()) + ",";
    json += "\"logical_led_count\":" + String(_leds.logicalNumLeds()) + ",";
    json += "\"contact_poi\":"      + String(_cfg.contactPoi ? "true" : "false") + ",";
    json += "\"running_app_size\":" + String(running ? (unsigned)running->size : 0) + ",";
    json += "\"next_ota_size\":"    + String(next ? (unsigned)next->size : 0) + ",";
    json += "\"fs_offset\":"        + String(fsPart ? (unsigned)fsPart->address : 0) + ",";
    json += "\"fs_size\":"          + String(fsPart ? (unsigned)fsPart->size : 0) + ",";
    json += "\"battery_mv\":"       + String(_batMonitor.mv()) + ",";
    json += "\"battery_pct\":"      + String(_batMonitor.pct()) + ",";
    json += "\"sync_enabled\":"     + String(_cfg.syncEnabled ? "true" : "false") + ",";
    json += "\"sync_mask\":"        + String(_cfg.syncMask) + ",";
    json += "\"sync_channel\":"     + String(firstSyncChannel(_cfg.syncMask)) + ",";
    json += "\"sync_ready\":"       + String((_sync && _sync->isReady()) ? "true" : "false") + ",";
    json += "\"sync_if\":\""        + String(_sync ? _sync->wifiInterfaceName() : "-") + "\",";
    json += "\"rssi\":"             + String(_apMode ? 0 : WiFi.RSSI()) + ",";
    json += "\"fs\":{\"u\":" + String(_fsMounted ? (unsigned long)LittleFS.usedBytes() : 0);
    json += ",\"t\":" + String(_fsMounted ? (unsigned long)LittleFS.totalBytes() : 0);
    json += ",\"mounted\":" + String(_fsMounted ? "true" : "false") + "}";
    json += "}";
    _server.send(200, "application/json", json);
}

String WifiControl::wledStateJson() {
    bool on = _player.isLoaded() || _effectPlayer.isRunning();
    uint8_t bri = auraBrightnessToWled(_cfg.brightness);
    uint8_t fx = _effectPlayer.isRunning() ? _cfg.effectId : 0;
    if (fx > 25) fx = 0;

    int sx = _cfg.effectSpeed > 255 ? 255 : _cfg.effectSpeed;

    String json;
    json.reserve(720);
    json += "{\"on\":";
    json += on ? "true" : "false";
    json += ",\"bri\":" + String(bri);
    json += ",\"transition\":0,\"ps\":-1,\"pl\":-1";
    json += ",\"nl\":{\"on\":false,\"dur\":60,\"mode\":1,\"tbri\":0,\"rem\":-1}";
    json += ",\"udpn\":{\"send\":false,\"recv\":false}";
    json += ",\"lor\":0,\"mainseg\":0";
    json += ",\"seg\":[{\"id\":0,\"start\":0,\"stop\":" + String(_leds.logicalNumLeds());
    json += ",\"len\":" + String(_leds.logicalNumLeds());
    json += ",\"grp\":1,\"spc\":0,\"of\":0,\"on\":";
    json += on ? "true" : "false";
    json += ",\"frz\":false,\"bri\":" + String(bri);
    json += ",\"col\":[";
    for (int i = 0; i < 3; i++) {
        if (i > 0) json += ",";
        json += "[" + String(_cfg.paletteR[i]) + "," + String(_cfg.paletteG[i]) + "," + String(_cfg.paletteB[i]) + "]";
    }
    json += "],\"fx\":" + String(fx);
    json += ",\"sx\":" + String(sx);
    json += ",\"ix\":" + String(_cfg.effectIntensity);
    json += ",\"pal\":" + String(_cfg.effectPaletteId);
    json += ",\"sel\":true,\"rev\":";
    json += _cfg.effectReverse ? "true" : "false";
    json += ",\"mi\":";
    json += _cfg.renderMirror ? "true" : "false";
    json += "}]}";
    return json;
}

String WifiControl::wledInfoJson() {
    auto st = _player.stats();
    uint16_t fpsX10 = _effectPlayer.isRunning() ? _effectPlayer.fpsX10() : st.fpsX10;
    int32_t rssi = (_apMode || WiFi.status() != WL_CONNECTED) ? 0 : WiFi.RSSI();
    uint8_t signal = _apMode ? 100 : wifiSignalPct(rssi);
    String deviceName = strlen(_wantedHostname) ? String(_wantedHostname) : String(_cfg.hostname);
    uint16_t logicalCount = _leds.logicalNumLeds();
    uint16_t physicalCount = _leds.physicalNumLeds();

    String json;
    json.reserve(920);
    json += "{\"ver\":\"0.14.4\",\"vid\":2403290,\"cn\":\"AuraX\"";
    json += ",\"release\":\"AuraX WLED discovery compatibility\"";
    json += ",\"name\":\"" + jsonEscape(deviceName) + "\"";
    json += ",\"brand\":\"AuraX\",\"product\":\"AuraX\",\"btype\":\"esp32s3\"";
    json += ",\"mac\":\"" + compactMac() + "\"";
    json += ",\"ip\":\"" + activeIP().toString() + "\"";
    json += ",\"arch\":\"esp32\",\"core\":\"arduino\",\"lwip\":0";
    json += ",\"freeheap\":" + String((unsigned long)ESP.getFreeHeap());
    json += ",\"uptime\":" + String((unsigned long)(millis() / 1000));
    json += ",\"opt\":0,\"str\":false,\"udpport\":21324,\"live\":false";
    json += ",\"lm\":\"\",\"lip\":\"\",\"ws\":-1";
    json += ",\"fxcount\":54,\"palcount\":30";
    json += ",\"leds\":{\"count\":" + String(logicalCount);
    json += ",\"physical_count\":" + String(physicalCount);
    json += ",\"logical_count\":" + String(logicalCount);
    json += ",\"contact_poi\":" + String(_cfg.contactPoi ? "true" : "false");
    json += ",\"fps\":" + String((fpsX10 + 5) / 10);
    json += ",\"maxpwr\":" + String(_cfg.mALimit);
    json += ",\"maxseg\":1,\"lc\":1,\"pwr\":0,\"rgbw\":false}";
    json += ",\"wifi\":{\"bssid\":\"\",\"rssi\":" + String(rssi);
    json += ",\"signal\":" + String(signal);
    json += ",\"channel\":" + String(WiFi.channel()) + "}";
    json += ",\"fs\":{\"u\":" + String(_fsMounted ? (unsigned long)LittleFS.usedBytes() : 0);
    json += ",\"t\":" + String(_fsMounted ? (unsigned long)LittleFS.totalBytes() : 0);
    json += ",\"pmt\":0}";
    json += ",\"ndc\":0,\"platform\":\"esp32\"}";
    return json;
}

String WifiControl::wledEffectsJson() {
    return "[\"Solid\",\"Android\",\"BPM\",\"Flow\",\"Gravcenter\",\"Gravfreq\",\"Chase 2\",\"Chase 3\",\"Chunchun\",\"Lake\",\"Meteor\",\"Noise 3\",\"Oscillate\",\"Ripple\",\"Running\",\"Strobe\",\"Fade\",\"Rainbow\",\"Twinkle\",\"Sparkle\",\"Fireworks\",\"Scanner\",\"Dual Scanner\",\"Theater Chase\",\"Color Wipe\",\"Juggle\",\"Sinelon\",\"Fire Flicker\",\"Plasma\",\"Gradient\",\"Breath\",\"Dots\",\"Counter Chase\",\"Split Chase\",\"Collision\",\"Saw\",\"Chevron\",\"Pulse Train\",\"Cross Waves\",\"Barber Pole\",\"Scan Bars\",\"Prism\",\"Spin\",\"Twist\",\"Chase\",\"Fire\"]";
}

String WifiControl::wledPalettesJson() {
    return "[\"Custom\",\"Rainbow\",\"Fire\",\"Ocean\",\"Forest\",\"Party\",\"Sunset\",\"Polar\",\"Lava\",\"Pastel\",\"Neon\",\"Candy\",\"Aurora\",\"Vintage\",\"Rainbow Stripe\",\"Blue Purple\",\"Pink Candy\",\"C9\",\"Tiamat\",\"Dry Wet\",\"Red Blue\",\"Yellow Green\",\"Purple Green\",\"Warm White\",\"Aqua Magenta\",\"Police\",\"Matrix\",\"Sakura\",\"Electric\",\"Amber Teal\"]";
}

void WifiControl::handleWledJson() {
    sendCorsHeaders();
    String json;
    json.reserve(1900);
    json += "{\"state\":";
    json += wledStateJson();
    json += ",\"info\":";
    json += wledInfoJson();
    json += ",\"effects\":";
    json += wledEffectsJson();
    json += ",\"palettes\":";
    json += wledPalettesJson();
    json += "}";
    _server.send(200, "application/json", json);
}

void WifiControl::handleWledInfo() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledInfoJson());
}

void WifiControl::handleWledState() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledStateJson());
}

void WifiControl::handleWledStatePost() {
    sendCorsHeaders();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }

    if (doc.containsKey("bri")) {
        _cfg.brightness = wledBrightnessToAura((uint16_t)(doc["bri"] | 255));
        if (_sync) {
            _sync->broadcastBrightness(_cfg.brightness);
        } else {
            _leds.setBrightness(_cfg.brightness);
            _player.setBrightness(_cfg.brightness);
        }
        scheduleRuntimeConfigSave();
    }

    if (doc.containsKey("on")) {
        bool on = doc["on"] | false;
        if (!on) {
            if (_sync) {
                _sync->broadcastStop();
                fanoutStop();
            } else {
                _effectPlayer.stop();
                _player.blackout();
            }
        } else if (!_player.isLoaded() && !_effectPlayer.isRunning()) {
            if (_cfg.autoStart == 1) {
                EffectParams p = effectParamsFromConfig(_cfg);
                if (_sync) {
                    _sync->broadcastEffect(p);
                    fanoutEffect(p);
                } else {
                    _effectPlayer.start(p);
                }
            } else {
                if (!validateProgramForPlay(String(_cfg.pixFile))) return;
                int64_t startUs = esp_timer_get_time();
                if (_sync) {
                    uint8_t slot = (uint8_t)slotForProgramPath(_cfg.pixFile);
                    int errPlay = _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, slot);
                    if (errPlay) {
                        _server.send(500, "text/plain", playerLoadErrorText(errPlay));
                        return;
                    }
                    fanoutProgramStart(slot, startUs);
                } else {
                    _effectPlayer.stop();
                    _player.stopTask();
                    int errLoad = _player.load(_cfg.pixFile);
                    if (errLoad) {
                        _server.send(500, "text/plain", playerLoadErrorText(errLoad));
                        return;
                    }
                    _player.scheduleStart(startUs);
                    _player.startTask();
                }
            }
        }
    }

    _server.send(200, "application/json", wledStateJson());
}

void WifiControl::handleWledEffects() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledEffectsJson());
}

void WifiControl::handleWledPalettes() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledPalettesJson());
}

void WifiControl::sendCorsHeaders() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    _server.sendHeader("Vary", "Origin, Access-Control-Request-Method, Access-Control-Request-Headers, Access-Control-Request-Private-Network");
}

void WifiControl::handleCorsOptions() {
    sendCorsHeaders();
    _server.send(204, "text/plain", "");
}

String WifiControl::firmwareStatusJson() const {
    String json;
    json.reserve(720);
    json += "{";
    json += "\"current_version\":\"" + String(AURAX_FW_VERSION) + "\",";
    json += "\"current_build\":" + String(AURAX_FW_BUILD) + ",";
    json += "\"manifest_url\":\"" + String(AURAX_UPDATE_MANIFEST_URL) + "\",";
    json += "\"releases_url\":\"" + String(AURAX_RELEASES_URL) + "\",";
    json += "\"checked\":" + String(_fwCheck.checked ? "true" : "false") + ",";
    json += "\"update_available\":" + String(_fwCheck.updateAvailable ? "true" : "false") + ",";
    json += "\"checked_at_ms\":" + String(_fwCheck.checkedAtMs) + ",";
    json += "\"remote_build\":" + String(_fwCheck.remoteBuild) + ",";
    json += "\"remote_size\":" + String(_fwCheck.remoteSize) + ",";
    json += "\"remote_version\":\"" + jsonEscape(String(_fwCheck.remoteVersion)) + "\",";
    json += "\"remote_url\":\"" + jsonEscape(String(_fwCheck.remoteUrl)) + "\",";
    json += "\"remote_page\":\"" + jsonEscape(String(_fwCheck.remotePage)) + "\",";
    json += "\"remote_notes\":\"" + jsonEscape(String(_fwCheck.remoteNotes)) + "\",";
    json += "\"error\":\"" + jsonEscape(String(_fwCheck.error)) + "\"";
    json += "}";
    return json;
}

void WifiControl::handleFirmwareStatus() {
    sendCorsHeaders();
    _server.send(200, "application/json", firmwareStatusJson());
}

bool WifiControl::checkFirmwareManifest(bool force) {
    uint32_t now = millis();
    if (!force && _fwCheck.checked && _fwCheck.error[0] == 0 &&
        now - _fwCheck.checkedAtMs < FW_AUTO_CHECK_INTERVAL_MS) {
        return true;
    }

    _fwCheck.checked = true;
    _fwCheck.checkedAtMs = now;
    _fwCheck.updateAvailable = false;
    _fwCheck.remoteBuild = 0;
    _fwCheck.remoteSize = 0;
    _fwCheck.remoteVersion[0] = 0;
    _fwCheck.remoteUrl[0] = 0;
    _fwCheck.remotePage[0] = 0;
    _fwCheck.remoteNotes[0] = 0;
    _fwCheck.error[0] = 0;

    if (_apMode || WiFi.status() != WL_CONNECTED) {
        strlcpy(_fwCheck.error, "Update check requires an internet WiFi connection.", sizeof(_fwCheck.error));
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(FW_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, AURAX_UPDATE_MANIFEST_URL)) {
        strlcpy(_fwCheck.error, "Could not start update check.", sizeof(_fwCheck.error));
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        String err = "Update manifest HTTP " + String(code);
        strlcpy(_fwCheck.error, err.c_str(), sizeof(_fwCheck.error));
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err != DeserializationError::Ok) {
        strlcpy(_fwCheck.error, "Update manifest is not valid JSON.", sizeof(_fwCheck.error));
        return false;
    }

    JsonObject latest = doc["latest"].as<JsonObject>();
    if (latest.isNull()) latest = doc.as<JsonObject>();

    const char* version = latest["version"] | "";
    const char* url = latest["url"] | "";
    const char* page = latest["page"] | AURAX_RELEASES_URL;
    const char* notes = latest["notes"] | "";
    uint32_t build = latest["build"] | 0;
    uint32_t size = latest["size"] | 0;

    if (build == 0 || strlen(version) == 0) {
        strlcpy(_fwCheck.error, "Update manifest is missing version/build.", sizeof(_fwCheck.error));
        return false;
    }

    _fwCheck.remoteBuild = build;
    _fwCheck.remoteSize = size;
    _fwCheck.updateAvailable = build > AURAX_FW_BUILD;
    strlcpy(_fwCheck.remoteVersion, version, sizeof(_fwCheck.remoteVersion));
    strlcpy(_fwCheck.remoteUrl, url, sizeof(_fwCheck.remoteUrl));
    strlcpy(_fwCheck.remotePage, page, sizeof(_fwCheck.remotePage));
    strlcpy(_fwCheck.remoteNotes, notes, sizeof(_fwCheck.remoteNotes));
    return true;
}

void WifiControl::handleFirmwareCheck() {
    sendCorsHeaders();
    bool force = _server.arg("force") == "1";
    bool ok = checkFirmwareManifest(force);
    _server.send(ok ? 200 : 503, "application/json", firmwareStatusJson());
}

void WifiControl::handleRescue() {
    sendCorsHeaders();
    if (!_sync || !_sync->isReady()) {
        _server.send(503, "text/plain", "ESP-NOW not ready");
        return;
    }

    String actionArg = _server.arg("action");
    actionArg.toLowerCase();
    uint8_t action = 0;
    if (actionArg == "ap" || actionArg == "force_ap") {
        action = SyncControl::RESCUE_FORCE_AP;
    } else if (actionArg == "reboot") {
        action = SyncControl::RESCUE_REBOOT;
    } else if (actionArg == "sta" || actionArg == "wifi" || actionArg == "retry") {
        action = SyncControl::RESCUE_STA_RETRY;
    }

    if (action == 0) {
        _server.send(400, "text/plain", "Use action=ap, action=reboot, or action=sta");
        return;
    }

    _sync->broadcastRescue(action);
    _server.send(200, "text/plain", "Rescue command sent");
}

void WifiControl::handleConfigGet() {
    StaticJsonDocument<2048> doc;
    doc["ledType"]  = _cfg.ledType;
    doc["numLeds"]  = _cfg.numLeds;
    doc["dataPin"]  = _cfg.dataPin;
    doc["clkPin"]   = _cfg.clkPin;
    doc["spiFrequencyMhz"] = _cfg.spiFrequencyMhz;
    doc["ssid"]     = _cfg.ssid;
    doc["password"] = _cfg.password;
    doc["pixFile"]    = _cfg.pixFile;
    const char* deviceName = strlen(_wantedHostname) ? _wantedHostname : _cfg.hostname;
    doc["deviceName"] = deviceName;
    doc["hostname"]   = deviceName;
    doc["brightness"] = _cfg.brightness;
    doc["tempo"]       = _cfg.tempo;
    doc["endBehavior"] = _cfg.endBehavior;
    doc["effectId"]        = _cfg.effectId;
    doc["effectSpeed"]     = _cfg.effectSpeed;
    doc["effectIntensity"] = _cfg.effectIntensity;
    doc["effectDotSize"]   = _cfg.effectDotSize;
    doc["effectPaletteId"] = _cfg.effectPaletteId;
    doc["effectReverse"]   = _cfg.effectReverse;
    doc["renderMirror"]    = _cfg.renderMirror;
    doc["contactPoi"]      = (bool)_cfg.contactPoi;
    doc["paletteSize"]     = _cfg.paletteSize;
    doc["mALimit"]  = _cfg.mALimit;
    doc["batPin"]              = _cfg.batPin;
    doc["batMultiplier"]       = _cfg.batMultiplier;
    doc["batCalibration"]      = _cfg.batCalibration;
    doc["batMinMv"]            = _cfg.batMinMv;
    doc["batMaxMv"]            = _cfg.batMaxMv;
    doc["batIntervalMs"]       = _cfg.batIntervalMs;
    doc["batAutoOff"]          = (bool)_cfg.batAutoOff;
    doc["batAutoOffThreshold"] = _cfg.batAutoOffThreshold;
    doc["syncEnabled"]         = (bool)_cfg.syncEnabled;
    doc["syncMask"]            = _cfg.syncMask;
    doc["syncChannel"]         = firstSyncChannel(_cfg.syncMask);
    doc["autoStart"]           = _cfg.autoStart;
    doc["fwVersion"]           = AURAX_FW_VERSION;
    doc["fwBuild"]             = AURAX_FW_BUILD;
    doc["updateManifestUrl"]   = AURAX_UPDATE_MANIFEST_URL;
    doc["releasesUrl"]         = AURAX_RELEASES_URL;
    JsonArray pR = doc.createNestedArray("paletteR");
    JsonArray pG = doc.createNestedArray("paletteG");
    JsonArray pB = doc.createNestedArray("paletteB");
    for (int i = 0; i < 4; i++) { pR.add(_cfg.paletteR[i]); pG.add(_cfg.paletteG[i]); pB.add(_cfg.paletteB[i]); }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WifiControl::handleEffectStart() {
    StaticJsonDocument<640> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    EffectParams p = {};
    int effectId = doc["id"] | 1;
    if (effectId != 1 && effectId != 2 && (effectId < 10 || effectId > 53)) effectId = 1;
    p.effectId = (uint8_t)effectId;
    int speed = doc["speed"] | 128;
    p.speed = normalizeEffectSpeedValue(speed);
    int intensity = doc["intensity"] | 128;
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    p.intensity = (uint8_t)intensity;
    int dotSize = doc["dotSize"] | 3;
    if (dotSize < 1) dotSize = 1;
    if (dotSize > 255) dotSize = 255;
    p.dotSize = (uint8_t)dotSize;
    if (p.dotSize < 1) p.dotSize = 1;
    uint16_t logicalCount = _leds.logicalNumLeds();
    if (p.dotSize > logicalCount) p.dotSize = logicalCount > 255 ? 255 : (uint8_t)logicalCount;
    int paletteId = doc["paletteId"] | 0;
    if (paletteId < 0) paletteId = 0;
    if (paletteId > 29) paletteId = 29;
    p.paletteId = (uint8_t)paletteId;
    p.reverse = (doc["reverse"] | (int)_cfg.effectReverse) ? 1 : 0;
    JsonArray palette = doc["colors"].as<JsonArray>();
    if (palette.isNull()) palette = doc["palette"].as<JsonArray>();
    p.paletteSize = 0;
    for (JsonObject c : palette) {
        if (p.paletteSize >= 4) break;
        p.palette[p.paletteSize].r = c["r"] | 255;
        p.palette[p.paletteSize].g = c["g"] | 0;
        p.palette[p.paletteSize].b = c["b"] | 0;
        p.paletteSize++;
    }
    if (p.paletteSize == 0) { p.palette[0] = {255, 0, 0}; p.paletteSize = 1; }
    bool persist = doc["persist"] | true;
    bool relay = doc["relay"] | true;

    // Zastavit přehrávač před zápisem do LittleFS — vyhnout se souběžnému přístupu
    if (_player.isLoaded()) {
        _player.stopTask();
        _player.unload();
    }

    // Uložit do konfigurace
    _cfg.autoStart     = 1;
    _cfg.effectId        = p.effectId;
    _cfg.effectSpeed     = p.speed;
    _cfg.effectIntensity = p.intensity;
    _cfg.effectDotSize   = p.dotSize;
    _cfg.effectPaletteId = p.paletteId;
    _cfg.effectReverse   = p.reverse;
    _cfg.paletteSize     = p.paletteSize;
    for (int i = 0; i < p.paletteSize; i++) {
        _cfg.paletteR[i] = p.palette[i].r;
        _cfg.paletteG[i] = p.palette[i].g;
        _cfg.paletteB[i] = p.palette[i].b;
    }
    if (persist) {
        saveRuntimeConfig();
    } else {
        scheduleRuntimeConfigSave();
    }

    if (_sync && relay) {
        _sync->broadcastEffect(p);
        fanoutEffect(p);
    } else {
        _effectPlayer.apply(p);
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleEffectStop() {
    _effectPlayer.stop();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleConfigPost() {
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    bool wifiChanged = false;
    bool hostnameProvided = doc.containsKey("deviceName") || doc.containsKey("hostname");
    char postedHostname[32] = "";
    if (hostnameProvided) {
        const char* rawHost = doc["deviceName"] | "";
        if (!strlen(rawHost)) rawHost = doc["hostname"] | "";
        strlcpy(postedHostname, rawHost, sizeof(postedHostname));
        normalizeHostname(postedHostname, sizeof(postedHostname));
        const char* configuredHost = strlen(_wantedHostname) ? _wantedHostname : _cfg.hostname;
        if (strcmp(postedHostname, configuredHost) != 0) wifiChanged = true;
    }
    if (doc.containsKey("ssid") &&
        strcmp(doc["ssid"] | "", _cfg.ssid) != 0)
        wifiChanged = true;
    if (doc.containsKey("password") &&
        strcmp(doc["password"] | "", _cfg.password) != 0)
        wifiChanged = true;
    bool contactPoiChanged = false;

    _cfg.ledType = doc["ledType"] | _cfg.ledType;
    {
        uint16_t n = doc["numLeds"] | _cfg.numLeds;
        if (n >= 1 && n <= 2048) _cfg.numLeds = n;
    }
    _cfg.dataPin = doc["dataPin"] | _cfg.dataPin;
    _cfg.clkPin  = doc["clkPin"]  | _cfg.clkPin;
    {
        int spiMhz = doc["spiFrequencyMhz"] | _cfg.spiFrequencyMhz;
        if (spiMhz < 1) spiMhz = 1;
        if (spiMhz > 20) spiMhz = 20;
        _cfg.spiFrequencyMhz = (uint8_t)spiMhz;
    }
    strlcpy(_cfg.ssid,     doc["ssid"]     | _cfg.ssid,     sizeof(_cfg.ssid));
    strlcpy(_cfg.password, doc["password"] | _cfg.password, sizeof(_cfg.password));
    strlcpy(_cfg.pixFile,  doc["pixFile"]  | _cfg.pixFile,  sizeof(_cfg.pixFile));
    if (hostnameProvided) {
        strlcpy(_cfg.hostname, postedHostname, sizeof(_cfg.hostname));
        strlcpy(_wantedHostname, postedHostname, sizeof(_wantedHostname));
    }
    _cfg.brightness = doc["brightness"] | _cfg.brightness;
    _player.setBrightness(_cfg.brightness);
    _cfg.tempo        = doc["tempo"]        | _cfg.tempo;
    _cfg.endBehavior  = doc["endBehavior"]  | _cfg.endBehavior;
    _player.setTempo(_cfg.tempo);
    _player.setEndBehavior(_cfg.endBehavior);
    if (doc.containsKey("contactPoi")) {
        uint8_t nextContactPoi = doc["contactPoi"] ? 1 : 0;
        contactPoiChanged = nextContactPoi != _cfg.contactPoi;
        _cfg.contactPoi = nextContactPoi;
    }
    _leds.setContactPoi(_cfg.contactPoi != 0);
    {
        int effectId = doc["effectId"] | _cfg.effectId;
        if (effectId == 1 || effectId == 2 || (effectId >= 10 && effectId <= 53)) {
            _cfg.effectId = (uint8_t)effectId;
        }
    }
    {
        int speed = doc["effectSpeed"] | _cfg.effectSpeed;
        _cfg.effectSpeed = normalizeEffectSpeedValue(speed);
    }
    {
        int intensity = doc["effectIntensity"] | _cfg.effectIntensity;
        if (intensity < 0) intensity = 0;
        if (intensity > 255) intensity = 255;
        _cfg.effectIntensity = (uint8_t)intensity;
    }
    {
        int dotSize = doc["effectDotSize"] | _cfg.effectDotSize;
        if (dotSize < 1) dotSize = 1;
        uint16_t logicalCount = _leds.logicalNumLeds();
        int maxSize = logicalCount > 255 ? 255 : logicalCount;
        if (dotSize > maxSize) dotSize = maxSize;
        _cfg.effectDotSize = (uint8_t)dotSize;
    }
    {
        int paletteId = doc["effectPaletteId"] | _cfg.effectPaletteId;
        if (paletteId < 0) paletteId = 0;
        if (paletteId > 29) paletteId = 29;
        _cfg.effectPaletteId = (uint8_t)paletteId;
    }
    if (doc.containsKey("effectReverse")) _cfg.effectReverse = doc["effectReverse"] ? 1 : 0;
    if (doc.containsKey("renderMirror")) _cfg.renderMirror = doc["renderMirror"] ? 1 : 0;
    _leds.setReverse(_cfg.effectReverse != 0);
    _leds.setMirror(_cfg.renderMirror != 0);
    {
        int paletteSize = doc["paletteSize"] | _cfg.paletteSize;
        if (paletteSize < 1) paletteSize = 1;
        if (paletteSize > 4) paletteSize = 4;
        _cfg.paletteSize = (uint8_t)paletteSize;
    }
    _cfg.mALimit  = doc["mALimit"]  | _cfg.mALimit;
    _leds.setCurrentLimit(_cfg.mALimit, 60);
    _cfg.batPin              = doc["batPin"]              | _cfg.batPin;
    _cfg.batMultiplier       = doc["batMultiplier"]       | _cfg.batMultiplier;
    _cfg.batCalibration      = doc["batCalibration"]      | _cfg.batCalibration;
    {
        uint16_t mn = doc["batMinMv"] | _cfg.batMinMv;
        uint16_t mx = doc["batMaxMv"] | _cfg.batMaxMv;
        if (mx > mn) { _cfg.batMinMv = mn; _cfg.batMaxMv = mx; }
    }
    {
        uint32_t iv = doc["batIntervalMs"] | _cfg.batIntervalMs;
        if (iv >= 100) {
            _cfg.batIntervalMs = iv < BATTERY_MIN_INTERVAL_MS ? BATTERY_MIN_INTERVAL_MS : iv;
        }
    }
    if (doc.containsKey("batAutoOff")) _cfg.batAutoOff = doc["batAutoOff"] ? 1 : 0;
    _cfg.batAutoOffThreshold = doc["batAutoOffThreshold"] | _cfg.batAutoOffThreshold;
    if (doc.containsKey("syncEnabled")) _cfg.syncEnabled = doc["syncEnabled"] ? 1 : 0;
    if (doc.containsKey("syncMask")) {
        uint16_t mask = doc["syncMask"] | _cfg.syncMask;
        _cfg.syncMask = mask & 0x03FF;
    } else if (doc.containsKey("syncChannel")) {
        uint8_t ch = doc["syncChannel"] | 0;
        _cfg.syncMask = (ch >= 1 && ch <= 10) ? (uint16_t)(1u << (ch - 1)) : 0;
    }
    if (_sync) {
        _sync->setSyncMask(_cfg.syncMask);
        _sync->setSyncEnabled(_cfg.syncEnabled != 0);
    }
    _batMonitor.resetInterval();  // re-measure with new settings
    JsonArray pR = doc["paletteR"], pG = doc["paletteG"], pB = doc["paletteB"];
    for (int i = 0; i < 4; i++) {
        if (i < (int)pR.size()) _cfg.paletteR[i] = pR[i];
        if (i < (int)pG.size()) _cfg.paletteG[i] = pG[i];
        if (i < (int)pB.size()) _cfg.paletteB[i] = pB[i];
    }
    if (contactPoiChanged) {
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
        _leds.clear();
    }
    String configResponse = "Ulozeno";
    if (contactPoiChanged && _fsMounted && strlen(_cfg.pixFile) && LittleFS.exists(_cfg.pixFile)) {
        String validationError;
        if (!programMatchesLedCount(String(_cfg.pixFile), _leds.logicalNumLeds(), validationError)) {
            configResponse += ". " + validationError;
        }
    }

    if (saveRuntimeConfig()) {
        if (wifiChanged) {
            _server.send(200, "text/plain", configResponse + " - restartuji WiFi");
            delay(750);
            esp_restart();
            return;
        }
        _server.send(200, "text/plain", configResponse);
        return;
        if (wifiChanged) {
            _server.send(200, "text/plain", "Uloženo — restartuji WiFi");
            delay(750);
            esp_restart();
        } else {
            _server.send(200, "text/plain", "Uloženo");
        }
    } else {
        _server.send(500, "text/plain", "Chyba zápisu");
    }
}

// ── UDP discovery ──────────────────────────────────────────────────────────────

void WifiControl::announce() {
    char buf[112];
    snprintf(buf, sizeof(buf), "AURAX %s %s %04x %u %d %u %u",
        _cfg.hostname, activeIP().toString().c_str(), softApSuffix(),
        _batMonitor.pct(), _apMode ? 0 : (int)WiFi.RSSI(), (unsigned)_cfg.syncEnabled, (unsigned)_cfg.syncMask);
    _udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    _udp.write((uint8_t*)buf, strlen(buf));
    _udp.endPacket();
    _lastAnnounceMs = millis();
}

void WifiControl::receivePeers() {
    int len = 0;
    while ((len = _udp.parsePacket()) > 0) {
    char buf[128] = {};
    _udp.read(buf, sizeof(buf) - 1);

    char* cmd      = strtok(buf, " ");
    if (cmd && strcmp(cmd, "AURAX?") == 0) {
        announce();
        continue;
    }
    char* host     = strtok(nullptr, " ");
    char* ip       = strtok(nullptr, " ");
    char* chipHex  = strtok(nullptr, " ");
    char* batPctStr     = strtok(nullptr, " ");
    char* rssiStr       = strtok(nullptr, " ");
    char* syncEnabledStr = strtok(nullptr, " ");
    char* syncMaskStr   = strtok(nullptr, " ");
    if (!cmd || strcmp(cmd, "AURAX") != 0 || !host || !ip) continue;

    // Ignorovat vlastní broadcast — kontrola vždy podle IP, nezávisle na hostname
    IPAddress senderIp;
    senderIp.fromString(ip);
    if (senderIp == activeIP()) continue;

    uint16_t senderChipId   = chipHex   ? (uint16_t)strtoul(chipHex, nullptr, 16) : 0;
    uint8_t  senderBatPct   = batPctStr ? (uint8_t)atoi(batPctStr) : 0;
    int8_t   senderRssi     = rssiStr   ? (int8_t)atoi(rssiStr)    : 0;
    uint8_t  senderSyncEnabled = syncEnabledStr ? (uint8_t)atoi(syncEnabledStr) : 0;
    uint16_t senderSyncMask = syncMaskStr ? (uint16_t)strtoul(syncMaskStr, nullptr, 10) : 0;
    if (syncEnabledStr && !syncMaskStr) {
        senderSyncMask = (uint16_t)strtoul(syncEnabledStr, nullptr, 10);
        senderSyncEnabled = senderSyncMask ? 1 : 0;
    }
    uint16_t myChipId     = softApSuffix();

    if (strcmp(host, _cfg.hostname) == 0) {
        // Konflikt: přejmenuje se zařízení s vyšším chip ID (deterministické)
        if (myChipId < senderChipId) {
            announce();  // já mám nižší ID, vyhrávám — připomenutím donutím druhého k přejmenování
            return;
        }
        char newHost[32];
        snprintf(newHost, sizeof(newHost), "%s-%04x", _cfg.hostname, myChipId);
        LOG("[mdns] conflict with %s (id=%04x > mine=%04x), renaming to %s.local\n",
            senderIp.toString().c_str(), senderChipId, myChipId, newHost);
        strlcpy(_cfg.hostname, newHost, sizeof(_cfg.hostname));
        MDNS.end();
        mdnsBegin(_cfg.hostname);
        announce();
        return;
    }

    // Aktualizovat existující peer nebo přidat nový. Primárně podle chip ID,
    // potom podle IP. Hostname se může při konfliktu změnit a nesmí schovat
    // další připojené zařízení se stejným nebo podobným jménem.
    for (int i = 0; i < _peerCount; i++) {
        bool sameIp = _peers[i].ip == senderIp;
        bool sameHost = strcmp(_peers[i].hostname, host) == 0;
        if (sameIp || sameHost) {
            strlcpy(_peers[i].hostname, host, sizeof(_peers[i].hostname));
            _peers[i].ip = senderIp;
            _peers[i].lastSeenMs  = millis();
            _peers[i].chipId      = senderChipId;
            _peers[i].batPct      = senderBatPct;
            _peers[i].rssi        = senderRssi;
            _peers[i].syncEnabled = senderSyncEnabled;
            _peers[i].syncMask    = senderSyncMask;
            goto next_packet;
        }
    }
    if (_peerCount < MAX_PEERS) {
        strlcpy(_peers[_peerCount].hostname, host, sizeof(_peers[_peerCount].hostname));
        _peers[_peerCount].ip = senderIp;
        _peers[_peerCount].lastSeenMs  = millis();
        _peers[_peerCount].chipId      = senderChipId;
        _peers[_peerCount].batPct      = senderBatPct;
        _peers[_peerCount].rssi        = senderRssi;
        _peers[_peerCount].syncEnabled = senderSyncEnabled;
        _peers[_peerCount].syncMask    = senderSyncMask;
        _peerCount++;
        LOG("[discovery] peer: %s (%s)\n", host, ip);
    }
next_packet:
    ;
    }
}

void WifiControl::expirePeers() {
    uint32_t now = millis();
    for (int i = 0; i < _peerCount; ) {
        if (now - _peers[i].lastSeenMs > PEER_EXPIRE_MS) {
            LOG("[discovery] expired: %s\n", _peers[i].hostname);
            bool wasBlockingWanted = (strcmp(_peers[i].hostname, _wantedHostname) == 0);
            _peers[i] = _peers[--_peerCount];  // swap with last

            // Pokud jsme měli konflikt s tímto peerem, zkusíme znovu získat chtěný hostname
            if (wasBlockingWanted && strcmp(_cfg.hostname, _wantedHostname) != 0) {
                strlcpy(_cfg.hostname, _wantedHostname, sizeof(_cfg.hostname));
                MDNS.end();
                mdnsBegin(_cfg.hostname);
                LOG("[mdns] reclaimed http://%s.local\n", _cfg.hostname);
                announce();
            }
        } else {
            i++;
        }
    }
}

void WifiControl::sortPeers() {
    for (int i = 1; i < _peerCount; i++) {
        Peer key = _peers[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = asciiCaseCompare(key.hostname, _peers[j].hostname);
            if (cmp > 0) break;
            if (cmp == 0 && ipCompare(key.ip, _peers[j].ip) >= 0) break;
            _peers[j + 1] = _peers[j];
            j--;
        }
        _peers[j + 1] = key;
    }
}

void WifiControl::handlePeers() {
    sendCorsHeaders();
    if (_staServicesStarted) {
        const char* query = "AURAX?";
        _udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
        _udp.write((const uint8_t*)query, strlen(query));
        _udp.endPacket();
        IPAddress ip = WiFi.localIP();
        IPAddress mask = WiFi.subnetMask();
        IPAddress broadcast((uint8_t)(ip[0] | ~mask[0]),
                            (uint8_t)(ip[1] | ~mask[1]),
                            (uint8_t)(ip[2] | ~mask[2]),
                            (uint8_t)(ip[3] | ~mask[3]));
        if (broadcast != IPAddress(255, 255, 255, 255)) {
            _udp.beginPacket(broadcast, DISCOVERY_PORT);
            _udp.write((const uint8_t*)query, strlen(query));
            _udp.endPacket();
        }
        uint32_t until = millis() + 400;
        while ((int32_t)(millis() - until) < 0) {
            receivePeers();
            delay(5);
        }
        expirePeers();
    }
    sortPeers();
    String json = "[";
    for (int i = 0; i < _peerCount; i++) {
        if (i > 0) json += ",";
        json += "{\"hostname\":\""    + String(_peers[i].hostname)    + "\","
              + "\"ip\":\""         + _peers[i].ip.toString()       + "\","
              + "\"chip_id\":"      + String(_peers[i].chipId)      + ","
              + "\"bat_pct\":"      + String(_peers[i].batPct)      + ","
              + "\"rssi\":"         + String(_peers[i].rssi)        + ","
              + "\"sync_enabled\":" + String(_peers[i].syncEnabled ? "true" : "false") + ","
              + "\"sync_mask\":"    + String(_peers[i].syncMask)    + ","
              + "\"sync_channel\":" + String(firstSyncChannel(_peers[i].syncMask)) + "}";
    }
    json += "]";
    _server.send(200, "application/json", json);
}

void WifiControl::handleOta() {
    HTTPUpload& up = _server.upload();
    if (up.status == UPLOAD_FILE_START) {
        _player.stopTask();
        LOG("[ota] start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            LOG("[ota] begin failed\n");
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize)
            LOG("[ota] write error\n");
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true))
            LOG("[ota] done: %u bytes\n", up.totalSize);
        else
            LOG("[ota] end failed\n");
    }
}
