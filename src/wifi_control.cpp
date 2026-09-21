#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include "version.h"
#include "wled_fx_effect.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <mbedtls/base64.h>
#include <stdlib.h>
#include <string.h>
#include "web_html.h"

static constexpr uint16_t WLED_REALTIME_PORT = 21324;
static constexpr uint16_t DDP_REALTIME_PORT = 4048;
static constexpr uint16_t REALTIME_PACKET_MAX = 1472;
static constexpr uint32_t REALTIME_TIMEOUT_MS = 1500;
static constexpr size_t PROGRAM_UPLOAD_BUFFER_BYTES = 16 * 1024;
static constexpr size_t PROGRAM_UPLOAD_COMMIT_BYTES = 512 * 1024;
static constexpr size_t CHUNK_UPLOAD_COMMIT_BYTES = 512 * 1024;
static constexpr size_t PROGRAM_STORAGE_WRITE_SLICE_BYTES = 4 * 1024;
static constexpr char CHUNK_UPLOAD_TEMP_PATH[] = "/.aurax-upload.part";
static constexpr uint16_t PROGRAM_UPLOAD_STREAM_PORT = 4211;
static constexpr uint32_t PROGRAM_UPLOAD_STREAM_MAGIC = 0x31505541;   // "AUP1"
static constexpr uint32_t PROGRAM_UPLOAD_STREAM_REPLY = 0x314B4F41;   // "AOK1"
static constexpr size_t PROGRAM_UPLOAD_STREAM_FRAME_BYTES = 32 * 1024;
static constexpr size_t PROGRAM_UPLOAD_STREAM_BUFFER_BYTES = 32 * 1024;
static constexpr uint32_t PROGRAM_UPLOAD_STREAM_SETTLE_MS = 4;
static constexpr uint8_t PROGRAM_UPLOAD_STREAM_RECOVERY_ATTEMPTS = 3;

static bool writeFileFully(File& file, const uint8_t* data, size_t length, size_t& written) {
    size_t offset = 0;
    uint8_t retries = 0;
    while (offset < length) {
        size_t request = min(length - offset, PROGRAM_STORAGE_WRITE_SLICE_BYTES);
        size_t count = file.write(data + offset, request);
        if (count > 0) {
            offset += count;
            written += count;
            retries = 0;
            delay(1);
            continue;
        }
        file.flush();
        delay(5);
        if (++retries >= 40) return false;
    }
    return true;
}

static uint32_t readMemoryDw(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void writeMemoryDw(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static bool readClientFully(WiFiClient& client, uint8_t* data, size_t length,
                            uint32_t timeoutMs) {
    size_t offset = 0;
    uint32_t lastDataMs = millis();
    while (offset < length) {
        int available = client.available();
        if (available > 0) {
            size_t amount = min(length - offset, (size_t)available);
            int count = client.read(data + offset, amount);
            if (count > 0) {
                offset += (size_t)count;
                lastDataMs = millis();
                continue;
            }
        }
        if (!client.connected() || millis() - lastDataMs >= timeoutMs) return false;
        delay(1);
    }
    return true;
}

static bool sendUploadStreamReply(WiFiClient& client, uint32_t status, size_t received) {
    uint8_t reply[12];
    writeMemoryDw(reply, PROGRAM_UPLOAD_STREAM_REPLY);
    writeMemoryDw(reply + 4, status);
    writeMemoryDw(reply + 8, (uint32_t)received);
    size_t offset = 0;
    uint32_t startedAt = millis();
    while (offset < sizeof(reply) && client.connected()) {
        size_t count = client.write(reply + offset, sizeof(reply) - offset);
        if (count > 0) {
            offset += count;
            startedAt = millis();
        } else {
            if (millis() - startedAt >= 2000) return false;
            delay(1);
        }
    }
    return offset == sizeof(reply);
}

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
        return "Pixel count mismatch: the program does not match this device.";
    }
    return "load failed: " + String(err);
}

static String programPxMismatchText(uint32_t programPx, uint16_t devicePx) {
    return "Pixel count mismatch: the program uses " + String(programPx) +
           " pixels, but the device is configured for " + String(devicePx) + " pixels.";
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

static bool programMatchesLedCount(fs::FS& storage, const String& path, uint16_t ledCount, String& error) {
    File f = storage.open(path, "r");
    if (!f) {
        error = "Program validation failed: cannot open file";
        return false;
    }

    const size_t fileBytes = f.size();
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
            (void)startTime; (void)endTime; (void)isLast;
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
            uint64_t rawBytes = (uint64_t)width * (uint64_t)height * 4u;
            if (height == 0 || frequency == 0 || codec > 2 ||
                (uint64_t)dataOffset + dataSize > fileBytes ||
                (uint64_t)decodedOffset + rawBytes > decodedBytes) {
                f.close();
                error = "Program validation failed: incomplete or invalid AXP data";
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

static size_t fileSizeOf(fs::FS& storage, const char* path) {
    File f = storage.open(path, "r");
    if (!f) return 0;
    size_t size = f.size();
    f.close();
    return size;
}

static size_t waitForFileSize(fs::FS& storage, const char* path, size_t expected,
                              uint32_t timeoutMs = 500) {
    uint32_t startedAt = millis();
    size_t size = fileSizeOf(storage, path);
    while (size != expected && millis() - startedAt < timeoutMs) {
        delay(10);
        size = fileSizeOf(storage, path);
    }
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

static int collectPrograms(fs::FS& storage, ProgramEntry* entries, int maxEntries) {
    int count = 0;
    File root = storage.open("/");
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

static bool applyProgramOrder(fs::FS& storage, ProgramEntry* entries, int count, AppConfig* cfg) {
    String selected = cfg ? String(cfg->pixFile) : "";
    String tempPaths[32];
    int selectedIndex = -1;

    for (int i = 0; i < count; i++) {
        if (entries[i].path == selected) selectedIndex = i;
        tempPaths[i] = "/__aurax_tmp_" + String(i);
        if (storage.exists(tempPaths[i])) storage.remove(tempPaths[i]);
        if (!storage.rename(entries[i].path, tempPaths[i])) return false;
    }

    for (int i = 0; i < count; i++) {
        String finalPath = numberedProgramPath((uint16_t)(i + 1), entries[i].displayName);
        if (storage.exists(finalPath)) storage.remove(finalPath);
        if (!storage.rename(tempPaths[i], finalPath)) return false;
        if (cfg && i == selectedIndex) strlcpy(cfg->pixFile, finalPath.c_str(), sizeof(cfg->pixFile));
    }
    return true;
}

static bool renumberPrograms(fs::FS& storage, AppConfig* cfg) {
    ProgramEntry entries[32];
    int count = collectPrograms(storage, entries, 32);
    if (count == 0) return true;
    return applyProgramOrder(storage, entries, count, cfg);
}

static bool programPathForSlot(fs::FS& storage, uint16_t slot, String& out) {
    ProgramEntry entries[32];
    int count = collectPrograms(storage, entries, 32);
    if (slot < 1 || slot > count) return false;
    out = entries[slot - 1].path;
    return true;
}

static uint16_t slotForProgramPath(fs::FS& storage, const String& path) {
    ProgramEntry entries[32];
    int count = collectPrograms(storage, entries, 32);
    for (int i = 0; i < count; i++) {
        if (entries[i].path == path) return (uint16_t)(i + 1);
    }
    return 0;
}

static String nextUploadProgramPath(fs::FS& storage, const String& filename) {
    ProgramEntry entries[32];
    int count = collectPrograms(storage, entries, 32);
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

static int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
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
        // Some WLED-compatible apps filter discovery by the official WLED brand
        // marker. The root page is still AuraX; this is only for discovery.
        MDNS.addServiceTxt("wled", "tcp", "brand", "WLED");
        MDNS.addServiceTxt("wled", "tcp", "product", "WLED");
        MDNS.addServiceTxt("wled", "tcp", "type", "wled");
        MDNS.addServiceTxt("wled", "tcp", "name", mdnsHost);
        MDNS.addServiceTxt("wled", "tcp", "ip", activeIP().toString().c_str());
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
                         ProgramStorage& programStorage, SyncControl* sync, bool fsMounted)
    : _player(player), _effectPlayer(effectPlayer), _leds(leds), _cfg(cfg),
      _programStorage(programStorage), _sync(sync), _fsMounted(fsMounted) {
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
    if (_realtimeUdpStarted) stopRealtimeUdp();
    _wledRealtimeUdp.begin(WLED_REALTIME_PORT);
    _ddpUdp.begin(DDP_REALTIME_PORT);
    _realtimeUdpStarted = true;
    LOG("[realtime] WLED UDP %u, DDP %u ready\n", WLED_REALTIME_PORT, DDP_REALTIME_PORT);
}

void WifiControl::stopRealtimeUdp() {
    if (!_realtimeUdpStarted) return;
    _wledRealtimeUdp.stop();
    _ddpUdp.stop();
    _realtimeUdpStarted = false;
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
    return _programStorage.ready();
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
    stopRealtimeUdp();
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

    stopRealtimeUdp();
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
    stopRealtimeUdp();
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
    _server.on("/json/cfg", HTTP_OPTIONS, [this]() { handleCorsOptions(); });
    _server.on("/json/cfg", HTTP_GET, [this]() { handleWledConfig(); });
    _server.on("/json/cfg", HTTP_POST, [this]() { handleWledConfigPost(); });
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
    _server.on("/program/upload/start", HTTP_POST, [this]() { handleProgramUploadStart(); });
    _server.on("/program/upload/chunk", HTTP_POST, [this]() { handleProgramUploadChunk(); });
    _server.on("/program/upload/finish", HTTP_POST, [this]() { handleProgramUploadFinish(); });
    _server.on("/program/upload/abort", HTTP_POST, [this]() { handleProgramUploadAbort(); });
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
        bool ok = _programStorage.erasePrograms();
        if (ok) {
            _cfg.pixFile[0] = '\0';
            saveRuntimeConfig();
        }
        _server.send(ok ? 200 : 500, "text/plain",
            ok ? "Program storage erased" : "Program storage erase failed");
    });

    _server.on("/upload", HTTP_POST,
        [this]() {
            if (_uploadFile) _uploadFile.close();
            if (!storageReady()) {
                _server.send(503, "text/plain", "Storage unavailable. Format storage first.");
                return;
            }
            if (!_uploadError) {
                String validationError;
                if (!programMatchesLedCount(_programStorage.fs(), String(_cfg.pixFile), _leds.logicalNumLeds(), validationError)) {
                    _programStorage.fs().remove(_cfg.pixFile);
                    _server.send(400, "text/plain", validationError);
                    return;
                }
            }
            if (_uploadError) {
                if (_uploadPath.length()) _programStorage.fs().remove(_uploadPath);
                _server.send(500, "text/plain", "Upload failed: not enough program storage space");
            } else {
                _server.send(200, "text/plain", "OK: uploaded as " + String(_cfg.pixFile));
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
            if (!storageReady()) {
                _uploadError = true;
                return;
            }
            if (up.status == UPLOAD_FILE_START) {
                _uploadError = false;
                _player.unload();
                if (_programStorage.fs().exists(_cfg.pixFile)) _programStorage.fs().remove(_cfg.pixFile);
                _uploadFile = _programStorage.fs().open(_cfg.pixFile, "w");
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
            releaseProgramUploadBatch();
            if (!storageReady()) {
                _server.send(503, "text/plain", "Storage unavailable. Format storage first.");
                return;
            }
            if (_uploadError) {
                if (_uploadPath.length()) _programStorage.fs().remove(_uploadPath);
                _server.send(_uploadErrorStatus, "text/plain",
                             _uploadErrorMessage.length() ? _uploadErrorMessage : "Program upload failed");
            } else {
                String validationError;
                if (!programMatchesLedCount(_programStorage.fs(), _uploadPath, _leds.logicalNumLeds(), validationError)) {
                    if (_uploadPath.length()) _programStorage.fs().remove(_uploadPath);
                    _server.send(400, "text/plain", validationError);
                    return;
                }
                strlcpy(_cfg.pixFile, _uploadPath.c_str(), sizeof(_cfg.pixFile));
                _cfg.autoStart = 0;
                renumberPrograms(_programStorage.fs(), &_cfg);
                saveRuntimeConfig();
                _server.send(200, "text/plain", "OK: uploaded " + _uploadPath);
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
            if (!storageReady()) {
                _uploadError = true;
                _uploadErrorStatus = 503;
                _uploadErrorMessage = "Program storage is unavailable";
                return;
            }
            if (up.status == UPLOAD_FILE_START) {
                releaseProgramUploadBatch();
                _uploadError = false;
                _uploadErrorStatus = 500;
                _uploadErrorMessage = "";
                _uploadPath = nextUploadProgramPath(_programStorage.fs(), up.filename);
                _uploadWritten = 0;
                _uploadCommittedBytes = 0;
                _uploadExpectedBytes = 0;
                _uploadEscaped = _server.header("X-AuraX-Transfer-Encoding").equalsIgnoreCase("escape-cr");
                _uploadEscapePending = false;
                String programLedsHeader = _server.header("X-AuraX-Num-Leds");
                if (programLedsHeader.length()) {
                    uint32_t programLeds = strtoul(programLedsHeader.c_str(), nullptr, 10);
                    if (programLeds != 0 && programLeds != _leds.logicalNumLeds()) {
                        _uploadError = true;
                        _uploadErrorStatus = 400;
                        _uploadErrorMessage = programPxMismatchText(programLeds, _leds.logicalNumLeds());
                        return;
                    }
                }
                size_t existingSize = fileSizeOf(_programStorage.fs(), _uploadPath.c_str());
                size_t total = _programStorage.totalBytes();
                size_t used = _programStorage.usedBytes();
                _uploadMaxBytes = (total > used ? total - used : 0) + existingSize;
                String expectedHeader = _server.header("X-AuraX-File-Size");
                if (expectedHeader.length()) {
                    unsigned long long expected = strtoull(expectedHeader.c_str(), nullptr, 10);
                    if (expected > (unsigned long long)SIZE_MAX) {
                        _uploadError = true;
                        _uploadErrorStatus = 413;
                        _uploadErrorMessage = "Program file is too large for this device";
                        return;
                    }
                    _uploadExpectedBytes = (size_t)expected;
                }
                if (_uploadExpectedBytes > _uploadMaxBytes) {
                    _uploadError = true;
                    _uploadErrorStatus = 413;
                    _uploadErrorMessage = "Program needs " + String((unsigned)_uploadExpectedBytes) +
                                          " bytes, but only " + String((unsigned)_uploadMaxBytes) +
                                          " bytes are free";
                    return;
                }
                _player.stopTask();
                _player.unload();
                if (_programStorage.fs().exists(_uploadPath)) _programStorage.fs().remove(_uploadPath);
                _uploadFile = _programStorage.fs().open(_uploadPath, "w");
                if (!_uploadFile) {
                    LOGLN("[program] upload open failed");
                    _uploadError = true;
                    _uploadErrorStatus = 500;
                    _uploadErrorMessage = "Cannot create the program file on " + String(_programStorage.typeName());
                    return;
                }
                _uploadBatchCapacity = 32 * 1024;
                _uploadBatch = static_cast<uint8_t*>(
                    heap_caps_malloc(_uploadBatchCapacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                if (!_uploadBatch) {
                    _uploadBatch = static_cast<uint8_t*>(malloc(_uploadBatchCapacity));
                }
                if (!_uploadBatch) {
                    _uploadFile.close();
                    _programStorage.fs().remove(_uploadPath);
                    _uploadError = true;
                    _uploadErrorStatus = 503;
                    _uploadErrorMessage = "Not enough memory to buffer the program upload";
                    return;
                }
                if (!_programStorage.externalMounted() &&
                    !_uploadFile.setBufferSize(PROGRAM_UPLOAD_BUFFER_BYTES)) {
                    LOGLN("[program] upload file buffer unavailable; using filesystem default");
                }
                LOG("[program] upload start: %s as %s free=%u\n", up.filename.c_str(), _uploadPath.c_str(), (unsigned)_uploadMaxBytes);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (_uploadFile && !_uploadError) {
                    const uint8_t* uploadData = up.buf;
                    size_t uploadSize = up.currentSize;
                    uint8_t decoded[HTTP_UPLOAD_BUFLEN];
                    if (_uploadEscaped) {
                        uploadSize = 0;
                        for (size_t i = 0; i < up.currentSize; i++) {
                            uint8_t value = up.buf[i];
                            if (_uploadEscapePending) {
                                if (value == 0xDC) decoded[uploadSize++] = 0x0D;
                                else if (value == 0xDD) decoded[uploadSize++] = 0xDB;
                                else {
                                    _uploadError = true;
                                    _uploadErrorStatus = 400;
                                    _uploadErrorMessage = "Invalid escaped program data";
                                    break;
                                }
                                _uploadEscapePending = false;
                            } else if (value == 0xDB) {
                                _uploadEscapePending = true;
                            } else {
                                decoded[uploadSize++] = value;
                            }
                        }
                        uploadData = decoded;
                    }
                    if (_uploadError) return;
                    if (_uploadWritten + _uploadBatchLength + uploadSize > _uploadMaxBytes) {
                        LOGLN("[program] upload rejected: file too large");
                        _uploadError = true;
                        _uploadErrorStatus = 413;
                        _uploadErrorMessage = "Program is larger than the available storage space";
                        _uploadFile.close();
                        _programStorage.fs().remove(_uploadPath);
                        return;
                    }
                    size_t sourceOffset = 0;
                    while (sourceOffset < uploadSize && !_uploadError) {
                        size_t room = _uploadBatchCapacity - _uploadBatchLength;
                        size_t amount = min(room, uploadSize - sourceOffset);
                        memcpy(_uploadBatch + _uploadBatchLength, uploadData + sourceOffset, amount);
                        _uploadBatchLength += amount;
                        sourceOffset += amount;
                        if (_uploadBatchLength == _uploadBatchCapacity && !flushProgramUploadBatch()) {
                            LOGLN("[program] upload write failed");
                            _uploadError = true;
                            _uploadErrorStatus = 507;
                            _uploadErrorMessage = "Storage write failed after " +
                                                  String((unsigned)_uploadWritten) +
                                                  " bytes. Check the XTSD module and its SPI settings.";
                        }
                    }
                    if (!_uploadError &&
                        _uploadWritten - _uploadCommittedBytes >= PROGRAM_UPLOAD_COMMIT_BYTES) {
                        _uploadFile.flush();
                        _uploadFile.close();
                        size_t storedBytes = waitForFileSize(_programStorage.fs(), _uploadPath.c_str(),
                                                             _uploadWritten);
                        if (storedBytes != _uploadWritten) {
                            _uploadError = true;
                            _uploadErrorStatus = 507;
                            _uploadErrorMessage = "Storage commit failed after " + String((unsigned)storedBytes) +
                                                  " of " + String((unsigned)_uploadWritten) + " bytes";
                            return;
                        }
                        _uploadFile = _programStorage.fs().open(_uploadPath, "a");
                        if (!_uploadFile) {
                            _uploadError = true;
                            _uploadErrorStatus = 507;
                            _uploadErrorMessage = "Cannot continue the program file after " +
                                                  String((unsigned)storedBytes) + " bytes";
                            return;
                        }
                        if (!_programStorage.externalMounted()) {
                            _uploadFile.setBufferSize(PROGRAM_UPLOAD_BUFFER_BYTES);
                        }
                        _uploadCommittedBytes = storedBytes;
                        delay(0);
                    }
                }
            } else if (up.status == UPLOAD_FILE_END) {
                if (_uploadEscaped && _uploadEscapePending && !_uploadError) {
                    _uploadError = true;
                    _uploadErrorStatus = 400;
                    _uploadErrorMessage = "Escaped program upload ended mid-byte";
                }
                if (_uploadFile && !_uploadError && !flushProgramUploadBatch()) {
                    _uploadError = true;
                    _uploadErrorStatus = 507;
                    _uploadErrorMessage = "Storage write failed after " +
                                          String((unsigned)_uploadWritten) + " bytes";
                }
                if (_uploadFile) {
                    _uploadFile.flush();
                    _uploadFile.close();
                    LOG("[program] upload done: %u bytes\n", up.totalSize);
                }
                size_t expectedBytes = _uploadExpectedBytes ? _uploadExpectedBytes : up.totalSize;
                if (!_uploadError && _uploadWritten != expectedBytes) {
                    _uploadError = true;
                    _uploadErrorStatus = 400;
                    _uploadErrorMessage = "Upload was incomplete: received " + String((unsigned)_uploadWritten) +
                                          " of " + String((unsigned)expectedBytes) + " bytes";
                }
                size_t storedBytes = waitForFileSize(_programStorage.fs(), _uploadPath.c_str(),
                                                     expectedBytes);
                if (!_uploadError && storedBytes != expectedBytes) {
                    _uploadError = true;
                    _uploadErrorStatus = 507;
                    _uploadErrorMessage = "Storage kept only " + String((unsigned)storedBytes) +
                                          " of " + String((unsigned)expectedBytes) +
                                          " bytes. Check the XTSD module and its power supply.";
                }
                releaseProgramUploadBatch();
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
    _server.on("/audio/start", HTTP_POST, [this]() { handleAudioReactiveStart(); });
    _server.on("/audio/data", HTTP_POST, [this]() { handleAudioReactiveData(); });
    _server.on("/audio/stop", HTTP_POST, [this]() { handleAudioReactiveStop(); });
    _server.on("/audio/settings", HTTP_POST, [this]() { handleAudioReactiveSettings(); });
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
                         Update.hasError() ? "Firmware update failed" : "OK: rebooting");
            delay(500);
            esp_restart();
        },
        [this]() { handleOta(); }
    );

    const char* trackedHeaders[] = {
        "X-AuraX-File-Size", "X-AuraX-Transfer-Encoding", "X-AuraX-Num-Leds"};
    _server.collectHeaders(trackedHeaders, 3);
    _server.begin();
    _programUploadServer.begin();
    _programUploadServer.setNoDelay(true);

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
        stopRealtimeUdp();
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
    processAudioGroup();
    receiveAudioStream();
    handleProgramUploadStream();
    _server.handleClient();
    receiveAudioStream();
    if (_audioReactiveActive) {
        if (!_effectPlayer.isAudioReactive()) {
            endAudioGroup();
            _groupReceiver.block();
            _groupFollower = false;
            _audioReactiveActive = false;
            _audioResumeEffect = false;
            _audioUdp.stop();
            _audioUdpStarted = false;
            _effectPlayer.clearAudioReactive();
        } else if (millis() - _audioLastPacketMs > 1500) {
            stopAudioReactive(true);
        }
    }
    if (_apActive) _dns.processNextRequest();
    receiveRealtimeUdp();
    maintainWifi();
    if (_sync) _sync->refreshWifiPeer();
    if (_batMonitor.update()) {
        _effectPlayer.stop();
        _player.blackout();
        LOG("[bat] auto-off: battery %u%% (<= %u%%)\n", _batMonitor.pct(), _cfg.batAutoOffThreshold);
    }
    processAudioGroup();
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
    if (!_apActive) {
        _server.send_P(200, "text/html", CLIENT_HTML);
        return;
    }
    // Mark only portal entry; opening the normal device URL keeps direct file upload.
    _server.sendHeader("Location", rootUrl() + "?aurax_captive=1");
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(302, "text/plain", "");
}

bool WifiControl::validateProgramForPlay(const String& path) {
    if (!storageReady()) {
        _server.send(503, "text/plain", "Program storage unavailable");
        return false;
    }
    if (!path.length() || !_programStorage.fs().exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return false;
    }
    String validationError;
    if (!programMatchesLedCount(_programStorage.fs(), path, _leds.logicalNumLeds(), validationError)) {
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
    if (!storageReady()) {
        _server.send(503, "text/plain", "Program storage unavailable");
        return;
    }
    bool relay = requestAllowsRelay();
    if (!validateProgramForPlay(String(_cfg.pixFile))) return;
    uint8_t slot = (uint8_t)slotForProgramPath(_programStorage.fs(), _cfg.pixFile);
    _effectPlayer.stop();
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    if (_sync && relay) {
        uint32_t totalAgeMs = (uint32_t)((esp_timer_get_time() - requestUs) / 1000);
        if (totalAgeMs > 30000) totalAgeMs = 30000;
        int err = totalAgeMs > 0
            ? _sync->broadcastPlayFromAge(_cfg.pixFile, _cfg.endBehavior, totalAgeMs, slot)
            : _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, slot);
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

void WifiControl::handleProgramUploadStart() {
    if (!storageReady()) {
        _server.send(503, "text/plain", "Program storage is unavailable");
        return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "Invalid upload request");
        return;
    }
    const char* filename = doc["filename"] | "program.axp";
    uint64_t requestedSize = doc["size"] | 0ULL;
    uint32_t declaredLedCount = doc["numLeds"] | 0U;
    if (requestedSize == 0 || requestedSize > (uint64_t)SIZE_MAX) {
        _server.send(400, "text/plain", "Program size is invalid");
        return;
    }
    if (declaredLedCount != 0 && declaredLedCount != _leds.logicalNumLeds()) {
        _server.send(400, "text/plain",
                     programPxMismatchText(declaredLedCount, _leds.logicalNumLeds()));
        return;
    }

    fs::FS& storage = _programStorage.fs();
    if (_chunkUploadFile) _chunkUploadFile.close();
    if (_chunkUploadActive && _chunkUploadPath.length()) storage.remove(_chunkUploadPath);
    storage.remove(CHUNK_UPLOAD_TEMP_PATH);

    size_t total = _programStorage.totalBytes();
    size_t used = _programStorage.usedBytes();
    size_t freeBytes = total > used ? total - used : 0;
    if (requestedSize > freeBytes) {
        _chunkUploadActive = false;
        _server.send(413, "text/plain",
                     "Program needs " + String((unsigned)requestedSize) +
                     " bytes, but only " + String((unsigned)freeBytes) + " bytes are free");
        return;
    }

    _effectPlayer.stop();
    _player.stopTask();
    _player.unload();

    _chunkUploadPath = CHUNK_UPLOAD_TEMP_PATH;
    _chunkUploadFinalPath = nextUploadProgramPath(storage, filename);
    _chunkUploadExpected = (size_t)requestedSize;
    _chunkUploadWritten = 0;
    _chunkUploadCommitted = 0;
    _chunkUploadFile = storage.open(_chunkUploadPath, "w");
    if (!_chunkUploadFile) {
        _chunkUploadActive = false;
        _server.send(507, "text/plain", "Cannot create a temporary program file");
        return;
    }
    if (!_programStorage.externalMounted()) {
        _chunkUploadFile.setBufferSize(PROGRAM_UPLOAD_BUFFER_BYTES);
    }
    _chunkUploadActive = true;

    StaticJsonDocument<192> response;
    response["path"] = _chunkUploadFinalPath;
    response["received"] = 0;
    response["total"] = _chunkUploadExpected;
    response["streamPort"] = PROGRAM_UPLOAD_STREAM_PORT;
    response["streamFrameBytes"] = PROGRAM_UPLOAD_STREAM_FRAME_BYTES;
    String json;
    serializeJson(response, json);
    _server.send(200, "application/json", json);
}

bool WifiControl::flushProgramUploadBatch() {
    if (_uploadBatchLength == 0) return true;
    size_t batchWritten = 0;
    bool ok = writeFileFully(_uploadFile, _uploadBatch, _uploadBatchLength, batchWritten);
    _uploadWritten += batchWritten;
    _uploadBatchLength = 0;
    if (ok) _uploadFile.flush();
    delay(2);
    return ok;
}

void WifiControl::releaseProgramUploadBatch() {
    if (_uploadBatch) heap_caps_free(_uploadBatch);
    _uploadBatch = nullptr;
    _uploadBatchLength = 0;
    _uploadBatchCapacity = 0;
}

void WifiControl::handleProgramUploadStream() {
    WiFiClient client = _programUploadServer.available();
    if (!client) return;

    client.setNoDelay(true);
    uint8_t header[16];
    if (!readClientFully(client, header, sizeof(header), 2500)) {
        client.stop();
        return;
    }

    uint32_t magic = readMemoryDw(header);
    uint32_t expected = readMemoryDw(header + 4);
    uint32_t offset = readMemoryDw(header + 8);
    uint32_t requestedFrameBytes = readMemoryDw(header + 12);
    if (magic != PROGRAM_UPLOAD_STREAM_MAGIC || requestedFrameBytes == 0 ||
        requestedFrameBytes > PROGRAM_UPLOAD_STREAM_FRAME_BYTES) {
        sendUploadStreamReply(client, 3, _chunkUploadWritten);
        client.stop();
        return;
    }
    if (!_chunkUploadActive || !_chunkUploadPath.length()) {
        sendUploadStreamReply(client, 1, _chunkUploadWritten);
        client.stop();
        return;
    }
    if (expected != _chunkUploadExpected || offset != _chunkUploadWritten) {
        sendUploadStreamReply(client, 2, _chunkUploadWritten);
        client.stop();
        return;
    }

    uint8_t* buffer = static_cast<uint8_t*>(
        heap_caps_malloc(PROGRAM_UPLOAD_STREAM_BUFFER_BYTES,
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buffer) buffer = static_cast<uint8_t*>(malloc(PROGRAM_UPLOAD_STREAM_BUFFER_BYTES));
    if (!buffer) {
        sendUploadStreamReply(client, 5, _chunkUploadWritten);
        client.stop();
        return;
    }
    if (!sendUploadStreamReply(client, 0, _chunkUploadWritten)) {
        free(buffer);
        client.stop();
        return;
    }

    fs::FS& storage = _programStorage.fs();
    bool failed = false;
    while (client.connected()) {
        uint8_t frameHeader[4];
        if (!readClientFully(client, frameHeader, sizeof(frameHeader), 15000)) {
            failed = true;
            break;
        }
        uint32_t frameBytes = readMemoryDw(frameHeader);
        if (frameBytes == 0) {
            uint32_t status = _chunkUploadWritten == _chunkUploadExpected ? 0 : 2;
            sendUploadStreamReply(client, status, _chunkUploadWritten);
            failed = status != 0;
            break;
        }
        if (frameBytes > requestedFrameBytes ||
            (uint64_t)_chunkUploadWritten + frameBytes > _chunkUploadExpected) {
            sendUploadStreamReply(client, 2, _chunkUploadWritten);
            failed = true;
            break;
        }
        if (!_chunkUploadFile) {
            _chunkUploadFile = storage.open(_chunkUploadPath, "a");
        }
        if (!_chunkUploadFile) {
            sendUploadStreamReply(client, 4, _chunkUploadWritten);
            failed = true;
            break;
        }

        size_t frameRemaining = frameBytes;
        uint8_t recoveryAttempts = 0;
        while (frameRemaining > 0) {
            size_t amount = min(frameRemaining, PROGRAM_UPLOAD_STREAM_BUFFER_BYTES);
            if (!readClientFully(client, buffer, amount, 15000)) {
                failed = true;
                break;
            }
            size_t bufferOffset = 0;
            while (bufferOffset < amount) {
                size_t request = amount - bufferOffset;
                size_t written = 0;
                bool writeOk = writeFileFully(_chunkUploadFile, buffer + bufferOffset,
                                              request, written);
                _chunkUploadWritten += written;
                frameRemaining -= written;
                bufferOffset += written;
                if (writeOk && written == request) break;

                if (_chunkUploadFile) {
                    _chunkUploadFile.flush();
                    _chunkUploadFile.close();
                }

                bool recovered = false;
                while (!recovered &&
                       recoveryAttempts < PROGRAM_UPLOAD_STREAM_RECOVERY_ATTEMPTS) {
                    recoveryAttempts++;
                    _programStorage.end();
                    delay(100 + (uint32_t)recoveryAttempts * 100);
                    if (!_programStorage.begin(_cfg, _fsMounted) ||
                        !_programStorage.externalMounted()) {
                        continue;
                    }
                    size_t storedBytes = waitForFileSize(_programStorage.fs(),
                                                         _chunkUploadPath.c_str(),
                                                         _chunkUploadWritten);
                    if (storedBytes != _chunkUploadWritten) continue;
                    _chunkUploadFile = _programStorage.fs().open(_chunkUploadPath, "a");
                    recovered = (bool)_chunkUploadFile;
                }
                if (!recovered) {
                    failed = true;
                    break;
                }
            }
            if (failed) break;
            // Let the XTSD controller finish background NAND work between frames.
            delay(PROGRAM_UPLOAD_STREAM_SETTLE_MS);
        }
        if (failed) {
            sendUploadStreamReply(client, 4, _chunkUploadWritten);
            break;
        }

        bool checkpoint = _chunkUploadWritten == _chunkUploadExpected ||
                          _chunkUploadWritten - _chunkUploadCommitted >=
                              CHUNK_UPLOAD_COMMIT_BYTES;
        if (checkpoint) {
            _chunkUploadFile.flush();
            _chunkUploadFile.close();
            size_t storedBytes = waitForFileSize(storage, _chunkUploadPath.c_str(),
                                                 _chunkUploadWritten);
            if (storedBytes != _chunkUploadWritten) {
                sendUploadStreamReply(client, 4, storedBytes);
                failed = true;
                break;
            }
            _chunkUploadCommitted = storedBytes;
            if (_chunkUploadWritten < _chunkUploadExpected) {
                _chunkUploadFile = storage.open(_chunkUploadPath, "a");
                if (!_chunkUploadFile) {
                    sendUploadStreamReply(client, 4, storedBytes);
                    failed = true;
                    break;
                }
            }
        }
        if (!sendUploadStreamReply(client, 0, _chunkUploadWritten)) {
            failed = true;
            break;
        }
    }

    free(buffer);
    client.stop();
    if (failed) {
        if (_chunkUploadFile) _chunkUploadFile.close();
        if (_programStorage.ready()) _programStorage.fs().remove(_chunkUploadPath);
        _chunkUploadActive = false;
        _chunkUploadWritten = 0;
        _chunkUploadCommitted = 0;
        if (_programStorage.externalMounted()) {
            _programStorage.end();
            delay(100);
            _programStorage.begin(_cfg, _fsMounted);
        }
    }
}

void WifiControl::handleProgramUploadChunk() {
    if (!_chunkUploadActive || !_chunkUploadPath.length()) {
        _server.send(409, "text/plain", "No program upload is active");
        return;
    }

    String offsetText = _server.arg("offset");
    uint64_t requestedOffset = strtoull(offsetText.c_str(), nullptr, 10);
    if (!offsetText.length() || requestedOffset > _chunkUploadWritten) {
        _server.send(409, "text/plain",
                     "Upload offset mismatch. Device expects " + String((unsigned)_chunkUploadWritten));
        return;
    }
    if (requestedOffset < _chunkUploadWritten) {
        StaticJsonDocument<128> response;
        response["received"] = _chunkUploadWritten;
        response["total"] = _chunkUploadExpected;
        String json;
        serializeJson(response, json);
        _server.send(200, "application/json", json);
        return;
    }

    String encoded = _server.arg("plain");
    if (!encoded.length()) {
        _server.send(400, "text/plain", "Upload chunk is empty");
        return;
    }
    size_t capacity = (encoded.length() / 4) * 3 + 3;
    uint8_t* decoded = static_cast<uint8_t*>(malloc(capacity));
    if (!decoded) {
        _server.send(503, "text/plain", "Not enough memory for the upload chunk");
        return;
    }
    size_t decodedLength = 0;
    int decodeResult = mbedtls_base64_decode(decoded, capacity, &decodedLength,
                                             reinterpret_cast<const unsigned char*>(encoded.c_str()),
                                             encoded.length());
    if (decodeResult != 0 || decodedLength == 0) {
        free(decoded);
        _server.send(400, "text/plain", "Upload chunk is not valid Base64 data");
        return;
    }
    if (_chunkUploadWritten + decodedLength > _chunkUploadExpected) {
        free(decoded);
        _server.send(413, "text/plain", "Upload exceeds the declared program size");
        return;
    }

    if (_chunkUploadWritten == 0 && decodedLength >= 16 &&
        readMemoryDw(decoded) == 0x31505841) {
        uint32_t version = readMemoryDw(decoded + 4);
        uint32_t commandCount = readMemoryDw(decoded + 8);
        uint32_t programLedCount = readMemoryDw(decoded + 12);
        if (version < 1 || version > 2 || commandCount == 0 || commandCount > 64) {
            free(decoded);
            _chunkUploadFile.close();
            _programStorage.fs().remove(_chunkUploadPath);
            _chunkUploadActive = false;
            _server.send(400, "text/plain", "Program validation failed: invalid AXP header");
            return;
        }
        if (programLedCount != 0 && programLedCount != _leds.logicalNumLeds()) {
            free(decoded);
            _chunkUploadFile.close();
            _programStorage.fs().remove(_chunkUploadPath);
            _chunkUploadActive = false;
            _server.send(400, "text/plain",
                         programPxMismatchText(programLedCount, _leds.logicalNumLeds()));
            return;
        }
    }

    fs::FS& storage = _programStorage.fs();
    if (!_chunkUploadFile) {
        _chunkUploadFile = storage.open(_chunkUploadPath, "a");
        if (_chunkUploadFile && !_programStorage.externalMounted()) {
            _chunkUploadFile.setBufferSize(PROGRAM_UPLOAD_BUFFER_BYTES);
        }
    }
    if (!_chunkUploadFile) {
        free(decoded);
        _server.send(507, "text/plain", "Cannot continue writing the program file");
        return;
    }
    size_t chunkWritten = 0;
    bool writeOk = writeFileFully(_chunkUploadFile, decoded, decodedLength, chunkWritten);
    free(decoded);
    if (!writeOk || chunkWritten != decodedLength) {
        _chunkUploadFile.close();
        size_t storedBytes = waitForFileSize(storage, _chunkUploadPath.c_str(), _chunkUploadWritten);
        storage.remove(_chunkUploadPath);
        _chunkUploadActive = false;
        _server.send(507, "text/plain",
                     "Storage write failed after " + String((unsigned)storedBytes) + " bytes");
        return;
    }
    _chunkUploadWritten += chunkWritten;

    bool checkpoint = _chunkUploadWritten == _chunkUploadExpected ||
                      _chunkUploadWritten - _chunkUploadCommitted >= CHUNK_UPLOAD_COMMIT_BYTES;
    if (checkpoint) {
        _chunkUploadFile.flush();
        _chunkUploadFile.close();
        size_t storedBytes = fileSizeOf(storage, _chunkUploadPath.c_str());
        if (storedBytes != _chunkUploadWritten) {
            storage.remove(_chunkUploadPath);
            _chunkUploadActive = false;
            _server.send(507, "text/plain",
                         "Storage write failed after " + String((unsigned)storedBytes) + " bytes");
            return;
        }
        _chunkUploadCommitted = storedBytes;
        if (_chunkUploadWritten < _chunkUploadExpected) {
            _chunkUploadFile = storage.open(_chunkUploadPath, "a");
            if (_chunkUploadFile && !_programStorage.externalMounted()) {
                _chunkUploadFile.setBufferSize(PROGRAM_UPLOAD_BUFFER_BYTES);
            }
            if (!_chunkUploadFile) {
                storage.remove(_chunkUploadPath);
                _chunkUploadActive = false;
                _server.send(507, "text/plain", "Cannot continue writing the program file");
                return;
            }
        }
    }

    StaticJsonDocument<128> response;
    response["received"] = _chunkUploadWritten;
    response["total"] = _chunkUploadExpected;
    String json;
    serializeJson(response, json);
    _server.send(200, "application/json", json);
}

void WifiControl::handleProgramUploadFinish() {
    if (!_chunkUploadActive || !_chunkUploadPath.length()) {
        _server.send(409, "text/plain", "No program upload is active");
        return;
    }
    fs::FS& storage = _programStorage.fs();
    if (_chunkUploadFile) {
        _chunkUploadFile.flush();
        _chunkUploadFile.close();
    }
    size_t storedBytes = waitForFileSize(storage, _chunkUploadPath.c_str(), _chunkUploadExpected);
    if (_chunkUploadWritten != _chunkUploadExpected || storedBytes != _chunkUploadExpected) {
        _server.send(400, "text/plain",
                     "Upload is incomplete: received " + String((unsigned)storedBytes) +
                     " of " + String((unsigned)_chunkUploadExpected) + " bytes");
        return;
    }

    String validationError;
    if (!programMatchesLedCount(storage, _chunkUploadPath, _leds.logicalNumLeds(), validationError)) {
        storage.remove(_chunkUploadPath);
        _chunkUploadActive = false;
        _server.send(400, "text/plain", validationError);
        return;
    }
    if (!storage.rename(_chunkUploadPath, _chunkUploadFinalPath)) {
        _server.send(507, "text/plain", "Cannot finalize the uploaded program file");
        return;
    }

    strlcpy(_cfg.pixFile, _chunkUploadFinalPath.c_str(), sizeof(_cfg.pixFile));
    _cfg.autoStart = 0;
    renumberPrograms(storage, &_cfg);
    saveRuntimeConfig();
    String finalPath = _cfg.pixFile;
    _chunkUploadActive = false;
    _chunkUploadPath = "";
    _chunkUploadFinalPath = "";
    _chunkUploadExpected = 0;
    _chunkUploadWritten = 0;
    _chunkUploadCommitted = 0;
    _server.send(200, "text/plain", "OK: uploaded " + finalPath);
}

void WifiControl::handleProgramUploadAbort() {
    if (_chunkUploadFile) _chunkUploadFile.close();
    if (_chunkUploadPath.length() && storageReady()) _programStorage.fs().remove(_chunkUploadPath);
    _chunkUploadActive = false;
    _chunkUploadPath = "";
    _chunkUploadFinalPath = "";
    _chunkUploadExpected = 0;
    _chunkUploadWritten = 0;
    _chunkUploadCommitted = 0;
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handlePrograms() {
    if (!storageReady()) {
        String json = "{\"total\":0,\"used\":0,\"free\":0,\"selected\":\"\",\"selected_slot\":0,"
                      "\"storage_mounted\":false,\"error\":\"Storage unavailable\",\"files\":[]}";
        _server.send(200, "application/json", json);
        return;
    }
    size_t total = _programStorage.totalBytes();
    size_t used = _programStorage.usedBytes();
    ProgramEntry entries[32];
    int count = collectPrograms(_programStorage.fs(), entries, 32);
    uint16_t selectedSlot = slotForProgramPath(_programStorage.fs(), _cfg.pixFile);
    String json = "{";
    json += "\"total\":" + String((unsigned)total) + ",";
    json += "\"used\":" + String((unsigned)used) + ",";
    json += "\"free\":" + String((unsigned)(total > used ? total - used : 0)) + ",";
    json += "\"selected\":\"" + jsonEscape(String(_cfg.pixFile)) + "\",";
    json += "\"selected_slot\":" + String(selectedSlot) + ",";
    json += "\"storage_mounted\":true,";
    json += "\"storage_type\":\"" + String(_programStorage.typeName()) + "\",";
    json += "\"external_storage_detected\":" + String(_programStorage.externalDetected() ? "true" : "false") + ",";
    json += "\"external_storage\":" + String(_programStorage.externalMounted() ? "true" : "false") + ",";
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
    if (!storageReady()) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }

    String path;
    uint16_t slot = (uint16_t)_server.arg("slot").toInt();
    if (slot > 0) {
        if (!programPathForSlot(_programStorage.fs(), slot, path)) {
            _server.send(404, "text/plain", "Program slot not found");
            return;
        }
    } else {
        path = sanitizeProgramPath(_server.arg("file"));
    }

    if (!path.length() || !_programStorage.fs().exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }

    File file = _programStorage.fs().open(path, "r");
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
    if (!storageReady()) {
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
        if (!programPathForSlot(_programStorage.fs(), slot, path)) {
            _server.send(404, "text/plain", "Program slot not found");
            return;
        }
    } else {
        path = sanitizeProgramPath(doc["file"] | "");
    }
    if (!_programStorage.fs().exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }
    strlcpy(_cfg.pixFile, path.c_str(), sizeof(_cfg.pixFile));
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramDelete() {
    if (!storageReady()) {
        _server.send(503, "text/plain", "Storage unavailable");
        return;
    }
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    String path = sanitizeProgramPath(doc["file"] | "");
    if (!_programStorage.fs().exists(path)) {
        _server.send(404, "text/plain", "Program not found");
        return;
    }
    if (path == String(_cfg.pixFile)) {
        _player.stopTask();
        _player.unload();
        strlcpy(_cfg.pixFile, "", sizeof(_cfg.pixFile));
        saveRuntimeConfig();
    }
    _programStorage.fs().remove(path);
    renumberPrograms(_programStorage.fs(), &_cfg);
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramReorder() {
    if (!storageReady()) {
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
    int count = collectPrograms(_programStorage.fs(), entries, 32);
    int idx = slot - 1;
    int target = idx + (direction < 0 ? -1 : 1);
    if (idx < 0 || idx >= count || target < 0 || target >= count || direction == 0) {
        _server.send(400, "text/plain", "Invalid order");
        return;
    }
    ProgramEntry tmp = entries[idx];
    entries[idx] = entries[target];
    entries[target] = tmp;
    if (!applyProgramOrder(_programStorage.fs(), entries, count, &_cfg)) {
        _server.send(500, "text/plain", "Reorder failed");
        return;
    }
    saveRuntimeConfig();
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleProgramStart() {
    int64_t requestUs = esp_timer_get_time();
    if (!storageReady()) {
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
    if (!programPathForSlot(_programStorage.fs(), slot, path)) {
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
        uint16_t slot = slotForProgramPath(_programStorage.fs(), _cfg.pixFile);
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
        uint8_t slot = (uint8_t)slotForProgramPath(_programStorage.fs(), _cfg.pixFile);
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
    json += "\"audio_reactive\":"   + String(_audioReactiveActive ? "true" : "false") + ",";
    json += "\"audio_packets_x10\":" + String(_audioReactiveActive ? _audioReceiver.packetRateX10(millis()) : 0) + ",";
    json += "\"audio_group_role\":\"" + String(_groupMaster ? "sender" : _groupFollower ? "receiver" : "none") + "\",";
    json += "\"audio_group_tx_frames\":" + String(_groupFramesSent) + ",";
    json += "\"audio_group_channel\":" + String(_sync ? _sync->wifiChannel() : 0) + ",";
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
    json += "\"storage_mounted\":"  + String(storageReady() ? "true" : "false") + ",";
    json += "\"storage_type\":\""    + String(_programStorage.typeName()) + "\",";
    json += "\"external_storage_detected\":" + String(_programStorage.externalDetected() ? "true" : "false") + ",";
    json += "\"external_storage\":" + String(_programStorage.externalMounted() ? "true" : "false") + ",";
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
    json += "\"fs\":{\"u\":" + String((unsigned long)_programStorage.usedBytes());
    json += ",\"t\":" + String((unsigned long)_programStorage.totalBytes());
    json += ",\"mounted\":" + String(storageReady() ? "true" : "false") + "}";
    json += "}";
    _server.send(200, "application/json", json);
}

String WifiControl::wledStateJson() {
    bool on = _player.isLoaded() || _effectPlayer.isRunning() || _realtimeActive;
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
    json += "{\"ver\":\"0.14.4\",\"vid\":2403290,\"cn\":\"WLED\"";
    json += ",\"release\":\"AuraX WLED discovery compatibility\"";
    json += ",\"name\":\"" + jsonEscape(deviceName) + "\"";
    json += ",\"brand\":\"WLED\",\"product\":\"WLED\",\"btype\":\"esp32s3\"";
    json += ",\"mac\":\"" + compactMac() + "\"";
    json += ",\"ip\":\"" + activeIP().toString() + "\"";
    json += ",\"arch\":\"esp32\",\"core\":\"arduino\",\"lwip\":0";
    json += ",\"freeheap\":" + String((unsigned long)ESP.getFreeHeap());
    json += ",\"uptime\":" + String((unsigned long)(millis() / 1000));
    json += ",\"opt\":0,\"str\":false,\"udpport\":21324,\"live\":";
    json += _realtimeActive ? "true" : "false";
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
    json += ",\"fs\":{\"u\":" + String((unsigned long)_programStorage.usedBytes());
    json += ",\"t\":" + String((unsigned long)_programStorage.totalBytes());
    json += ",\"pmt\":0}";
    json += ",\"ndc\":0,\"platform\":\"esp32\"}";
    return json;
}

String WifiControl::wledConfigJson() {
    String json;
    json.reserve(520);
    json += "{\"if\":{\"live\":{";
    json += "\"en\":true";
    json += ",\"port\":" + String(DDP_REALTIME_PORT);
    json += ",\"no-gc\":false";
    json += ",\"maxbri\":false";
    json += ",\"timeout\":25";
    json += ",\"dmx\":{\"mode\":4,\"uni\":1,\"addr\":1}";
    json += "}}";
    json += ",\"nw\":{\"ins\":[{\"name\":\"";
    json += jsonEscape(strlen(_wantedHostname) ? String(_wantedHostname) : String(_cfg.hostname));
    json += "\",\"ip\":\"";
    json += activeIP().toString();
    json += "\"}],\"mdns\":true}";
    json += "}";
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
    bool parsed = false;
    String body = _server.arg("plain");
    if (body.length()) {
        DeserializationError err = deserializeJson(doc, body);
        parsed = (err == DeserializationError::Ok);
    }
    if (!parsed && _server.hasArg("bri")) {
        doc["bri"] = _server.arg("bri").toInt();
        parsed = true;
    }
    if (!parsed && _server.hasArg("on")) parsed = true;
    if (_server.hasArg("on")) {
        String value = _server.arg("on");
        value.toLowerCase();
        doc["on"] = (value == "1" || value == "true" || value == "on");
    }
    if (!parsed) {
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
                    uint8_t slot = (uint8_t)slotForProgramPath(_programStorage.fs(), _cfg.pixFile);
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

void WifiControl::handleWledConfig() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledConfigJson());
}

void WifiControl::handleWledConfigPost() {
    sendCorsHeaders();
    _server.send(200, "application/json", wledConfigJson());
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
    if (_audioReactiveActive) {
        _server.send(409, "application/json",
                     "{\"error\":\"Stop Audio Reactive before checking for updates.\"}");
        return;
    }
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
    DynamicJsonDocument doc(2560);
    doc["ledType"]  = _cfg.ledType;
    doc["numLeds"]  = _cfg.numLeds;
    doc["dataPin"]  = _cfg.dataPin;
    doc["clkPin"]   = _cfg.clkPin;
    doc["spiFrequencyMhz"] = _cfg.spiFrequencyMhz;
    doc["externalStorageEnabled"] = (bool)_cfg.externalStorageEnabled;
    doc["storageSckPin"] = _cfg.storageSckPin;
    doc["storageMosiPin"] = _cfg.storageMosiPin;
    doc["storageMisoPin"] = _cfg.storageMisoPin;
    doc["storageCsPin"] = _cfg.storageCsPin;
    doc["storageSpiFrequencyMhz"] = _cfg.storageSpiFrequencyMhz;
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

    // Zastavit přehrávač před změnou trvalého stavu.
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

bool WifiControl::startAudioReactiveOutput() {
    if (_effectPlayer.isAudioReactive()) return true;
    _audioResumeEffect = _effectPlayer.isRunning();
    if (_player.isLoaded()) {
        _player.stopTask();
        _player.unload();
    }
    EffectParams p = effectParamsFromConfig(_cfg);
    p.effectId = EFFECT_AUDIO_REACTIVE;
    _effectPlayer.clearAudioReactive();
    _effectPlayer.start(p);
    if (_effectPlayer.isAudioReactive()) return true;
    _audioResumeEffect = false;
    return false;
}

void WifiControl::handleAudioReactiveStart() {
    const String session = _server.arg("session");
    if (session.length() != 32) {
        _server.send(400, "text/plain", "Invalid audio session");
        return;
    }
    for (unsigned int i = 0; i < session.length(); ++i) {
        if (hexNibble(session[i]) < 0) {
            _server.send(400, "text/plain", "Invalid audio session");
            return;
        }
    }
    if (_audioReactiveActive && _effectPlayer.isAudioReactive() &&
        millis() - _audioLastPacketMs <= 1500 && (session != _audioSession || _groupFollower)) {
        _server.send(409, "text/plain", "Audio Reactive is in use by another client");
        return;
    }
    const bool wantsStream = _server.arg("transport") == "udp2";
    if (wantsStream && !_audioUdpStarted) {
        if (!_audioUdp.begin(AudioStream::Port)) {
            _server.send(503, "text/plain", "Audio stream unavailable");
            return;
        }
        _audioUdpStarted = true;
    }
    if (!startAudioReactiveOutput()) {
        _audioUdp.stop();
        _audioUdpStarted = false;
        _server.send(503, "text/plain", "Audio Reactive could not start");
        return;
    }
    endAudioGroup();
    _groupReceiver.block();
    _groupMayResume = false;
    _groupFollower = false;
    _audioReactiveActive = true;
    _audioSession = session;
    _audioOwnerIp = _server.client().remoteIP();
    uint8_t token[16];
    for (unsigned int i = 0; i < 16; ++i)
        token[i] = (hexNibble(session[i * 2]) << 4) | hexNibble(session[i * 2 + 1]);
    _audioReceiver.reset(token);
    _audioLastAckMs = millis() - 200;
    _audioLastPacketMs = millis();
    _server.send(200, "text/plain", wantsStream ? "AXA2:4211:64" : "OK");
}

void WifiControl::receiveAudioStream() {
    if (_groupFollower || !_audioUdpStarted || !_audioReactiveActive || !_effectPlayer.isAudioReactive()) return;
    uint8_t packet[AudioStream::Bytes], latest[AudioStream::Bytes];
    bool accepted = false, beat = false;
    uint16_t replyPort = 0;
    // Bounded work and latest-wins: a burst can never monopolize the network task.
    for (unsigned int n = 0; n < 8; ++n) {
        int length = _audioUdp.parsePacket();
        if (length <= 0) break;
        IPAddress source = _audioUdp.remoteIP();
        uint16_t port = _audioUdp.remotePort();
        int read = _audioUdp.read(packet, sizeof(packet));
        _audioUdp.flush();
        if (source != _audioOwnerIp || length != (int)sizeof(packet) || read != length ||
            !_audioReceiver.accept(packet, read, millis())) continue;
        memcpy(latest, packet, sizeof(latest));
        beat |= packet[32] != 0;
        replyPort = port;
        accepted = true;
    }
    if (!accepted) return;
    latest[32] = beat ? 255 : 0;
    consumeAudioInput(latest + 28, latest + AudioStream::Header);
    _audioLastPacketMs = millis();
    if (millis() - _audioLastAckMs >= 200) {
        latest[2] = 'K';
        _audioUdp.beginPacket(_audioOwnerIp, replyPort);
        _audioUdp.write(latest, AudioStream::AckBytes);
        _audioUdp.endPacket();
        _audioLastAckMs = millis();
    }
}

void WifiControl::handleAudioReactiveData() {
    if (_groupFollower || !_audioReactiveActive || !_effectPlayer.isAudioReactive() ||
        _server.arg("session") != _audioSession) {
        _server.send(409, "text/plain", "Audio Reactive is not active");
        return;
    }

    String payload = _server.arg("plain");
    payload.trim();
    if (payload.length() != 12) {
        _server.send(400, "text/plain", "Invalid audio frame");
        return;
    }

    uint8_t values[6] = {};
    for (uint8_t i = 0; i < 6; i++) {
        int high = hexNibble(payload[i * 2]);
        int low = hexNibble(payload[i * 2 + 1]);
        if (high < 0 || low < 0) {
            _server.send(400, "text/plain", "Invalid audio frame");
            return;
        }
        values[i] = (uint8_t)((high << 4) | low);
    }
    if (values[0] != 1) {
        _server.send(400, "text/plain", "Unsupported audio frame version");
        return;
    }

    consumeAudioInput(values + 1, nullptr);
    _audioLastPacketMs = millis();
    _server.send(204, "text/plain", "");
}

void WifiControl::handleAudioReactiveStop() {
    if (!_groupFollower && _audioReactiveActive && _server.arg("session") == _audioSession)
        stopAudioReactive(_server.arg("restore") != "0");
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleAudioReactiveSettings() {
    if (_groupFollower || !_audioReactiveActive || !_effectPlayer.isAudioReactive() ||
        _server.arg("session") != _audioSession) {
        _server.send(409, "text/plain", "Audio Reactive is not active");
        return;
    }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "Invalid audio settings");
        return;
    }
    const char* keys[] = {"mode", "speed", "width", "decay", "brightness"};
    for (const char* key : keys) {
        if (!doc[key].is<int>() || doc[key].as<int>() < 0 ||
            doc[key].as<int>() > (strcmp(key, "mode") == 0 ? 9 : 100)) {
            _server.send(400, "text/plain", "Invalid audio parameter");
            return;
        }
    }
    if (!doc["mirror"].is<bool>() || !doc["colors"].is<JsonArray>() || doc["colors"].size() != 3) {
        _server.send(400, "text/plain", "Invalid audio colors or mirror");
        return;
    }
    AudioReactiveSettings settings;
    if (doc.containsKey("group") && !doc["group"].is<bool>()) {
        _server.send(400, "text/plain", "Invalid audio group setting");
        return;
    }
    bool group = doc["group"] | false;
    if (group && (!_sync || !_sync->audioGroupReady() || !_cfg.syncEnabled || !_cfg.syncMask)) {
        _server.send(409, "text/plain", "Enable SYNC and select a group before sharing audio");
        return;
    }
    if (doc.containsKey("response") && (!doc["response"].is<int>() ||
        doc["response"].as<int>() < 0 || doc["response"].as<int>() > 3)) {
        _server.send(400, "text/plain", "Invalid audio frequency range");
        return;
    }
    settings.response = doc["response"] | 0;
    settings.mode = doc["mode"];
    settings.speed = doc["speed"];
    settings.width = doc["width"];
    settings.decay = doc["decay"];
    settings.brightness = doc["brightness"];
    settings.mirror = doc["mirror"];
    for (uint8_t i = 0; i < 3; ++i) {
        const char* color = doc["colors"][i] | "";
        if (strlen(color) != 7 || color[0] != '#') {
            _server.send(400, "text/plain", "Invalid audio color");
            return;
        }
        uint8_t channels[3];
        for (uint8_t n = 0; n < 3; ++n) {
            int high = hexNibble(color[1 + n * 2]), low = hexNibble(color[2 + n * 2]);
            if (high < 0 || low < 0) {
                _server.send(400, "text/plain", "Invalid audio color");
                return;
            }
            channels[n] = (high << 4) | low;
        }
        settings.colors[i] = {channels[0], channels[1], channels[2]};
    }
    _audioSettings = settings;
    _effectPlayer.setAudioSettings(settings);
    if (group && !_groupMaster) beginAudioGroup();
    else if (!group) endAudioGroup();
    if (_groupMaster) updateAudioGroupSettings(settings);
    _server.send(204, "text/plain", "");
}

void WifiControl::stopAudioReactive(bool restorePreviousEffect, bool retireGroup) {
    bool restore = restorePreviousEffect && _audioResumeEffect && _effectPlayer.isAudioReactive();
    endAudioGroup();
    if (retireGroup) _groupReceiver.block();
    else _groupReceiver.expire();
    _groupFollower = false;
    _audioReactiveActive = false;
    _audioResumeEffect = false;
    _audioUdp.stop();
    _audioUdpStarted = false;
    if (_effectPlayer.isAudioReactive()) _effectPlayer.stop();
    _effectPlayer.clearAudioReactive();
    if (restore) _effectPlayer.start(effectParamsFromConfig(_cfg));
    _groupMayResume = !retireGroup;
    _groupResumeRevision = _effectPlayer.controlRevision();
}

void WifiControl::handleConfigPost() {
    DynamicJsonDocument doc(2560);
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
    const uint8_t oldLedType = _cfg.ledType;
    const uint16_t oldNumLeds = _cfg.numLeds;
    const uint8_t oldDataPin = _cfg.dataPin;
    const uint8_t oldClkPin = _cfg.clkPin;
    const uint8_t oldSpiFrequencyMhz = _cfg.spiFrequencyMhz;
    const uint8_t oldExternalStorageEnabled = _cfg.externalStorageEnabled;
    const uint8_t oldStorageSckPin = _cfg.storageSckPin;
    const uint8_t oldStorageMosiPin = _cfg.storageMosiPin;
    const uint8_t oldStorageMisoPin = _cfg.storageMisoPin;
    const uint8_t oldStorageCsPin = _cfg.storageCsPin;
    const uint8_t oldStorageSpiFrequencyMhz = _cfg.storageSpiFrequencyMhz;

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
    if (doc.containsKey("externalStorageEnabled")) {
        _cfg.externalStorageEnabled = doc["externalStorageEnabled"] ? 1 : 0;
    }
    {
        int pin = doc["storageSckPin"] | _cfg.storageSckPin;
        if (pin >= 0 && pin <= 48) _cfg.storageSckPin = (uint8_t)pin;
        pin = doc["storageMosiPin"] | _cfg.storageMosiPin;
        if (pin >= 0 && pin <= 48) _cfg.storageMosiPin = (uint8_t)pin;
        pin = doc["storageMisoPin"] | _cfg.storageMisoPin;
        if (pin >= 0 && pin <= 48) _cfg.storageMisoPin = (uint8_t)pin;
        pin = doc["storageCsPin"] | _cfg.storageCsPin;
        if (pin >= 0 && pin <= 48) _cfg.storageCsPin = (uint8_t)pin;
    }
    {
        int spiMhz = doc["storageSpiFrequencyMhz"] | _cfg.storageSpiFrequencyMhz;
        if (spiMhz < 1) spiMhz = 1;
        if (spiMhz > 50) spiMhz = 50;
        _cfg.storageSpiFrequencyMhz = (uint8_t)spiMhz;
    }
    const bool ledOutputChanged =
        _cfg.ledType != oldLedType ||
        _cfg.numLeds != oldNumLeds ||
        _cfg.dataPin != oldDataPin ||
        _cfg.clkPin != oldClkPin ||
        _cfg.spiFrequencyMhz != oldSpiFrequencyMhz;
    const bool storageChanged =
        _cfg.externalStorageEnabled != oldExternalStorageEnabled ||
        _cfg.storageSckPin != oldStorageSckPin ||
        _cfg.storageMosiPin != oldStorageMosiPin ||
        _cfg.storageMisoPin != oldStorageMisoPin ||
        _cfg.storageCsPin != oldStorageCsPin ||
        _cfg.storageSpiFrequencyMhz != oldStorageSpiFrequencyMhz;
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
    if (_cfg.mALimit == 0) _cfg.mALimit = DEFAULT_CURRENT_LIMIT_MA;
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
    String configResponse = "Saved";
    if (contactPoiChanged && storageReady() && strlen(_cfg.pixFile) && _programStorage.fs().exists(_cfg.pixFile)) {
        String validationError;
        if (!programMatchesLedCount(_programStorage.fs(), String(_cfg.pixFile), _leds.logicalNumLeds(), validationError)) {
            configResponse += ". " + validationError;
        }
    }

    if (saveRuntimeConfig()) {
        if (wifiChanged || ledOutputChanged || storageChanged) {
            _server.send(200, "text/plain", configResponse + " - restarting device");
            delay(750);
            esp_restart();
            return;
        }
        _server.send(200, "text/plain", configResponse);
        return;
        if (wifiChanged) {
            _server.send(200, "text/plain", "Saved - restarting WiFi");
            delay(750);
            esp_restart();
        } else {
            _server.send(200, "text/plain", "Saved");
        }
    } else {
        _server.send(500, "text/plain", "Failed to save settings");
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
    // The normal 400 ms discovery wait must not stall the audio receiver.
    // Passive peer discovery continues in handle(); audio requests use its cache.
    if (_staServicesStarted && !_audioReactiveActive) {
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
