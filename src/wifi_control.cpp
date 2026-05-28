#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <esp_wifi.h>
#include <mdns.h>
#include "web_html.h"

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

static String sanitizePixPath(const String& input) {
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
    if (!lower.endsWith(".pix")) clean += ".pix";
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
    String cleanPath = sanitizePixPath(displayName);
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
            if (lower.endsWith(".pix")) {
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
        tempPaths[i] = "/__aurax_tmp_" + String(i) + ".pix";
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
    return numberedProgramPath((uint16_t)(count + 1), programDisplayName(sanitizePixPath(filename)));
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

// ── WifiControl ───────────────────────────────────────────────────────────────

void WifiControl::mdnsBegin(const char* hostname) {
    const char* mdnsHost = strlen(hostname) ? hostname : "aurax";
    if (MDNS.begin(mdnsHost)) {
        MDNS.setInstanceName("AuraX");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("aurax", "tcp", 80);
        MDNS.addServiceTxt("aurax", "tcp", "hostname", mdnsHost);
        MDNS.addServiceTxt("aurax", "tcp", "ip", activeIP().toString().c_str());
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

WifiControl::WifiControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds, AppConfig& cfg, SyncControl* sync)
    : _player(player), _effectPlayer(effectPlayer), _leds(leds), _cfg(cfg), _sync(sync) {}

bool WifiControl::saveRuntimeConfig() {
    AppConfig saved = _cfg;
    if (strlen(_wantedHostname) > 0)
        strlcpy(saved.hostname, _wantedHostname, sizeof(saved.hostname));
    return saveConfig(saved);
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
    return (_cfg.wifiMode == WIFI_MODE_GROUP_CLIENT) ? _cfg.groupSsid : _cfg.ssid;
}

const char* WifiControl::staPassword() const {
    return (_cfg.wifiMode == WIFI_MODE_GROUP_CLIENT) ? _cfg.groupPassword : _cfg.password;
}

const char* WifiControl::groupPassword() const {
    return (strlen(_cfg.groupPassword) >= 8) ? _cfg.groupPassword : "";
}

bool WifiControl::connectSta(uint32_t timeoutMs) {
    _dns.stop();
    _apActive = false;
    _apMode = false;
    _apHadClient = false;
    _staServicesStarted = false;

    WiFi.disconnect(true);
    WiFi.setHostname(_cfg.hostname);
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    delay(100);
    WiFi.begin(staSsid(), staPassword());
    WiFi.setSleep(false);
    WiFi.setHostname(_cfg.hostname);
    _lastStaRetryMs = millis();
    _staDisconnectedSinceMs = _lastStaRetryMs;
    LOG("[wifi] connecting to %s", staSsid());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
        LOG("%c", '.');
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOGLN("\n[wifi] connect timeout");
        return false;
    }

    _staDisconnectedSinceMs = 0;
    _lastStaRetryMs = 0;
    LOG("\n[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WifiControl::startFallbackAp() {
    if (_apActive) return;

    _apMode = true;
    _apHadClient = false;
    WiFi.setHostname(_cfg.hostname);
    WiFi.mode(strlen(staSsid()) ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);

    char apSsid[32];
    snprintf(apSsid, sizeof(apSsid), "AuraX-%04X", (uint16_t)ESP.getEfuseMac());
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAPsetHostname(_cfg.hostname);
    WiFi.softAP(apSsid);
    _apActive = true;
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", WiFi.softAPIP());
    LOG("[wifi] AP fallback: SSID=%s IP=%s\n", apSsid, WiFi.softAPIP().toString().c_str());

    if (strlen(staSsid())) {
        WiFi.begin(staSsid(), staPassword());
        _lastStaRetryMs = millis();
        LOGLN("[wifi] background STA retry enabled until AP client connects");
    }
}

void WifiControl::startGroupMasterAp() {
    _dns.stop();
    _apMode = true;
    _apActive = true;
    _apHadClient = false;
    _staServicesStarted = false;

    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.setHostname(_cfg.hostname);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAPsetHostname(_cfg.hostname);
    bool ok = WiFi.softAP(_cfg.groupSsid, strlen(groupPassword()) ? groupPassword() : nullptr, GROUP_AP_CHANNEL);
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", WiFi.softAPIP());
    LOG("[wifi] group master: SSID=%s IP=%s %s\n",
        _cfg.groupSsid, WiFi.softAPIP().toString().c_str(), ok ? "" : "(softAP failed)");
}

void WifiControl::stopFallbackAp() {
    if (!_apActive) return;
    _dns.stop();
    WiFi.softAPdisconnect(true);
    _apActive = false;
    _apMode = false;
    WiFi.setHostname(_cfg.hostname);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    LOG("[wifi] AP disabled, STA IP: %s\n", WiFi.localIP().toString().c_str());
}

void WifiControl::startStaServices() {
    bool groupMaster = (_cfg.wifiMode == WIFI_MODE_GROUP_MASTER && _apActive);
    if (_staServicesStarted || (!groupMaster && WiFi.status() != WL_CONNECTED)) return;
    MDNS.end();
    mdnsBegin(_cfg.hostname);
    NBNS.begin(_cfg.hostname);
    _udp.begin(DISCOVERY_PORT);
    announce();
    ArduinoOTA.setHostname(_cfg.hostname);
    ArduinoOTA.begin();
    _staServicesStarted = true;
    LOG("[ota] ArduinoOTA ready\n");
}

bool WifiControl::begin(uint32_t timeoutMs) {
    WiFi.persistent(false);
    WiFi.softAPdisconnect(true);

    if (_cfg.wifiMode == WIFI_MODE_GROUP_MASTER) {
        LOGLN("[wifi] starting group master AP");
        startGroupMasterAp();
    } else if (strlen(staSsid()) == 0) {
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
        _effectPlayer.stop();
        _player.blackout();
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/power", HTTP_POST, [this]() { handlePower(); });
    _server.on("/status", HTTP_GET,  [this]() { handleStatus();    });
    _server.on("/config", HTTP_GET,  [this]() { handleConfigGet(); });
    _server.on("/config", HTTP_POST, [this]() { handleConfigPost(); });
    _server.on("/programs", HTTP_GET, [this]() { handlePrograms(); });
    _server.on("/program/select", HTTP_POST, [this]() { handleProgramSelect(); });
    _server.on("/program/delete", HTTP_POST, [this]() { handleProgramDelete(); });
    _server.on("/program/reorder", HTTP_POST, [this]() { handleProgramReorder(); });
    _server.on("/program/start", HTTP_POST, [this]() { handleProgramStart(); });
    _server.on("/sync", HTTP_POST, [this]() { handleSyncNow(); });
    _server.on("/reboot", HTTP_POST, [this]() {
        _server.send(200, "text/plain", "OK");
        delay(200);
        esp_restart();
    });

    _server.on("/upload", HTTP_POST,
        [this]() {
            if (_uploadFile) _uploadFile.close();
            if (_uploadError) {
                if (_uploadPath.length()) LittleFS.remove(_uploadPath);
                _server.send(500, "text/plain", "Chyba: nedostatek místa v LittleFS");
            } else {
                _server.send(200, "text/plain", "OK — soubor nahrán jako " + String(_cfg.pixFile));
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
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
            if (_uploadError) {
                if (_uploadPath.length()) LittleFS.remove(_uploadPath);
                _server.send(413, "text/plain", "Upload failed: not enough LittleFS space");
            } else {
                strlcpy(_cfg.pixFile, _uploadPath.c_str(), sizeof(_cfg.pixFile));
                _cfg.autoStart = 0;
                renumberPrograms(&_cfg);
                saveRuntimeConfig();
                _server.send(200, "text/plain", "OK: uploaded " + _uploadPath);
            }
        },
        [this]() {
            HTTPUpload& up = _server.upload();
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
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/effect",      HTTP_POST, [this]() { handleEffectStart(); });
    _server.on("/effect/stop", HTTP_GET,  [this]() { handleEffectStop();  });
    _server.on("/peers",  HTTP_GET,  [this]() { handlePeers(); });
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

    if (_cfg.wifiMode == WIFI_MODE_GROUP_MASTER) {
        startStaServices();
    } else if (_apMode) {
        mdnsBegin(_cfg.hostname);
    } else {
        startStaServices();
    }

    return true;
}

void WifiControl::maintainWifi() {
    if (_cfg.wifiMode == WIFI_MODE_GROUP_MASTER) {
        if (!_apActive) startGroupMasterAp();
        startStaServices();
        return;
    }
    if (strlen(staSsid()) == 0) return;

    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        _staDisconnectedSinceMs = 0;
        if (_apActive) {
            stopFallbackAp();
        }
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

    if (_apActive) {
        uint8_t clients = apClientCount();
        if (clients > 0) {
            if (!_apHadClient) {
                _apHadClient = true;
                WiFi.disconnect(false);
                LOG("[wifi] AP client connected (%u), pausing STA scan\n", clients);
            }
            return;
        }
        if (_apHadClient) {
            _apHadClient = false;
            _lastStaRetryMs = 0;
            LOGLN("[wifi] AP client left, resuming STA scan");
        }
        if (now - _lastStaRetryMs > STA_RETRY_INTERVAL_MS) {
            LOG("[wifi] AP fallback retry to %s\n", staSsid());
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(staSsid(), staPassword());
            _lastStaRetryMs = now;
        }
        return;
    }

    if (_staDisconnectedSinceMs == 0) _staDisconnectedSinceMs = now;
    if (now - _lastStaRetryMs > STA_RETRY_INTERVAL_MS) {
        LOG("[wifi] reconnecting to %s\n", staSsid());
        WiFi.begin(staSsid(), staPassword());
        _lastStaRetryMs = now;
    }

    if (!_apActive && now - _staDisconnectedSinceMs > STA_CONNECT_TIMEOUT_MS) {
        LOGLN("[wifi] reconnect timeout, starting AP fallback");
        startFallbackAp();
    }
}

void WifiControl::handle() {
    _server.handleClient();
    if (_apActive) _dns.processNextRequest();
    maintainWifi();
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

void WifiControl::handlePlay() {
    _effectPlayer.stop();
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    if (_sync) {
        _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slotForProgramPath(_cfg.pixFile));
    } else {
        int64_t startUs = esp_timer_get_time();
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
        _player.scheduleStart(startUs);
        _player.startTask(1);
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStop() {
    if (_sync) {
        _sync->broadcastStop();
    } else {
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handlePrograms() {
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

void WifiControl::handleProgramSelect() {
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
        path = sanitizePixPath(doc["file"] | "");
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
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    String path = sanitizePixPath(doc["file"] | "");
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
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    uint16_t slot = doc["slot"] | 0;
    String path;
    if (!programPathForSlot(slot, path)) {
        _server.send(404, "text/plain", "Program slot not found");
        return;
    }
    strlcpy(_cfg.pixFile, path.c_str(), sizeof(_cfg.pixFile));
    _cfg.autoStart = 0;
    saveRuntimeConfig();
    if (_sync) {
        _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slot);
    } else {
        int64_t startUs = esp_timer_get_time();
        _effectPlayer.stop();
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
        _player.scheduleStart(startUs);
        _player.startTask(1);
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handlePower() {
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    bool on = doc["on"] | false;
    if (!on) {
        if (_sync) {
            _sync->broadcastStop();
        } else {
            _effectPlayer.stop();
            _player.blackout();
        }
        _server.send(200, "text/plain", "OK");
        return;
    }

    if (_cfg.autoStart == 1) {
        EffectParams p = effectParamsFromConfig(_cfg);
        if (_sync) _sync->broadcastEffect(p);
        else _effectPlayer.start(p);
    } else {
        uint16_t slot = slotForProgramPath(_cfg.pixFile);
        if (_sync) {
            _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slot);
        } else {
            int64_t startUs = esp_timer_get_time();
            _effectPlayer.stop();
            _player.stopTask();
            int err = _player.load(_cfg.pixFile);
            if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
            _player.scheduleStart(startUs);
            _player.startTask(1);
        }
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleSyncNow() {
    if (_cfg.autoStart == 1) {
        EffectParams p = effectParamsFromConfig(_cfg);
        if (_sync) _sync->broadcastEffect(p);
        else _effectPlayer.start(p);
    } else {
        if (_sync) {
            _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior, 0, (uint8_t)slotForProgramPath(_cfg.pixFile));
        } else {
            int64_t startUs = esp_timer_get_time();
            _effectPlayer.stop();
            _player.stopTask();
            int err = _player.load(_cfg.pixFile);
            if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
            _player.scheduleStart(startUs);
            _player.startTask(1);
        }
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStatus() {
    auto st = _player.stats();
    char apSsid[32] = "";
    if (_apActive) {
        if (_cfg.wifiMode == WIFI_MODE_GROUP_MASTER)
            strlcpy(apSsid, _cfg.groupSsid, sizeof(apSsid));
        else
            snprintf(apSsid, sizeof(apSsid), "AuraX-%04X", (uint16_t)ESP.getEfuseMac());
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
    json += "\"wifi_mode\":"        + String(_cfg.wifiMode) + ",";
    json += "\"ap_ssid\":\""        + String(apSsid) + "\",";
    json += "\"ap_mode\":"          + String(_apMode ? "true" : "false") + ",";
    json += "\"battery_mv\":"       + String(_batMonitor.mv()) + ",";
    json += "\"battery_pct\":"      + String(_batMonitor.pct()) + ",";
    json += "\"sync_enabled\":"     + String(_cfg.syncEnabled ? "true" : "false") + ",";
    json += "\"sync_mask\":"        + String(_cfg.syncMask) + ",";
    json += "\"sync_channel\":"     + String(firstSyncChannel(_cfg.syncMask)) + ",";
    json += "\"rssi\":"             + String(_apMode ? 0 : WiFi.RSSI());
    json += "}";
    _server.send(200, "application/json", json);
}

void WifiControl::handleConfigGet() {
    StaticJsonDocument<1536> doc;
    doc["ledType"]  = _cfg.ledType;
    doc["numLeds"]  = _cfg.numLeds;
    doc["dataPin"]  = _cfg.dataPin;
    doc["clkPin"]   = _cfg.clkPin;
    doc["wifiMode"] = _cfg.wifiMode;
    doc["ssid"]     = _cfg.ssid;
    doc["password"] = _cfg.password;
    doc["groupSsid"] = _cfg.groupSsid;
    doc["groupPassword"] = _cfg.groupPassword;
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
    JsonArray pR = doc.createNestedArray("paletteR");
    JsonArray pG = doc.createNestedArray("paletteG");
    JsonArray pB = doc.createNestedArray("paletteB");
    for (int i = 0; i < 4; i++) { pR.add(_cfg.paletteR[i]); pG.add(_cfg.paletteG[i]); pB.add(_cfg.paletteB[i]); }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WifiControl::handleEffectStart() {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    EffectParams p = {};
    int effectId = doc["id"] | 1;
    if (effectId != 1 && effectId != 2 && (effectId < 10 || effectId > 25)) effectId = 1;
    p.effectId = (uint8_t)effectId;
    int speed = doc["speed"] | 100;
    if (speed < 10) speed = 10;
    if (speed > 1000) speed = 1000;
    p.speed = (uint16_t)speed;
    int intensity = doc["intensity"] | 128;
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    p.intensity = (uint8_t)intensity;
    int dotSize = doc["dotSize"] | 3;
    if (dotSize < 1) dotSize = 1;
    if (dotSize > 255) dotSize = 255;
    p.dotSize = (uint8_t)dotSize;
    if (p.dotSize < 1)                  p.dotSize = 1;
    if (p.dotSize > _cfg.numLeds)       p.dotSize = _cfg.numLeds;
    int paletteId = doc["paletteId"] | 0;
    if (paletteId < 0) paletteId = 0;
    if (paletteId > 32) paletteId = 32;
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
    if (persist) saveRuntimeConfig();

    if (_sync) {
        _sync->broadcastEffect(p);
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
    if (doc.containsKey("wifiMode") &&
        (uint8_t)(doc["wifiMode"] | _cfg.wifiMode) != _cfg.wifiMode)
        wifiChanged = true;
    if (doc.containsKey("groupSsid") &&
        strcmp(doc["groupSsid"] | "", _cfg.groupSsid) != 0)
        wifiChanged = true;
    if (doc.containsKey("groupPassword") &&
        strcmp(doc["groupPassword"] | "", _cfg.groupPassword) != 0)
        wifiChanged = true;

    _cfg.ledType = doc["ledType"] | _cfg.ledType;
    {
        uint16_t n = doc["numLeds"] | _cfg.numLeds;
        if (n >= 1 && n <= 2048) _cfg.numLeds = n;
    }
    _cfg.dataPin = doc["dataPin"] | _cfg.dataPin;
    _cfg.clkPin  = doc["clkPin"]  | _cfg.clkPin;
    if (doc.containsKey("wifiMode")) {
        int mode = doc["wifiMode"] | _cfg.wifiMode;
        if (mode < WIFI_MODE_NORMAL || mode > WIFI_MODE_GROUP_CLIENT) mode = WIFI_MODE_NORMAL;
        _cfg.wifiMode = (uint8_t)mode;
    }
    strlcpy(_cfg.ssid,     doc["ssid"]     | _cfg.ssid,     sizeof(_cfg.ssid));
    strlcpy(_cfg.password, doc["password"] | _cfg.password, sizeof(_cfg.password));
    strlcpy(_cfg.groupSsid, doc["groupSsid"] | _cfg.groupSsid, sizeof(_cfg.groupSsid));
    strlcpy(_cfg.groupPassword, doc["groupPassword"] | _cfg.groupPassword, sizeof(_cfg.groupPassword));
    if (strlen(_cfg.groupSsid) == 0)
        strlcpy(_cfg.groupSsid, GROUP_WIFI_SSID, sizeof(_cfg.groupSsid));
    if (strlen(_cfg.groupPassword) > 0 && strlen(_cfg.groupPassword) < 8)
        strlcpy(_cfg.groupPassword, GROUP_WIFI_PASSWORD, sizeof(_cfg.groupPassword));
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
    {
        int effectId = doc["effectId"] | _cfg.effectId;
        if (effectId == 1 || effectId == 2 || (effectId >= 10 && effectId <= 25)) {
            _cfg.effectId = (uint8_t)effectId;
        }
    }
    {
        int speed = doc["effectSpeed"] | _cfg.effectSpeed;
        if (speed < 10) speed = 10;
        if (speed > 1000) speed = 1000;
        _cfg.effectSpeed = (uint16_t)speed;
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
        int maxSize = _cfg.numLeds > 255 ? 255 : _cfg.numLeds;
        if (dotSize > maxSize) dotSize = maxSize;
        _cfg.effectDotSize = (uint8_t)dotSize;
    }
    {
        int paletteId = doc["effectPaletteId"] | _cfg.effectPaletteId;
        if (paletteId < 0) paletteId = 0;
        if (paletteId > 32) paletteId = 32;
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
        if (iv >= 100) _cfg.batIntervalMs = iv;
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

    if (saveRuntimeConfig()) {
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
        _cfg.hostname, activeIP().toString().c_str(), (uint16_t)ESP.getEfuseMac(),
        _batMonitor.pct(), _apMode ? 0 : (int)WiFi.RSSI(), (unsigned)_cfg.syncEnabled, (unsigned)_cfg.syncMask);
    _udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    _udp.write((uint8_t*)buf, strlen(buf));
    _udp.endPacket();
    _lastAnnounceMs = millis();
}

void WifiControl::receivePeers() {
    int len = _udp.parsePacket();
    if (len <= 0) return;
    char buf[80] = {};
    _udp.read(buf, sizeof(buf) - 1);

    char* cmd      = strtok(buf, " ");
    if (cmd && strcmp(cmd, "AURAX?") == 0) {
        announce();
        return;
    }
    char* host     = strtok(nullptr, " ");
    char* ip       = strtok(nullptr, " ");
    char* chipHex  = strtok(nullptr, " ");
    char* batPctStr     = strtok(nullptr, " ");
    char* rssiStr       = strtok(nullptr, " ");
    char* syncEnabledStr = strtok(nullptr, " ");
    char* syncMaskStr   = strtok(nullptr, " ");
    if (!cmd || strcmp(cmd, "AURAX") != 0 || !host || !ip) return;

    // Ignorovat vlastní broadcast — kontrola vždy podle IP, nezávisle na hostname
    IPAddress senderIp;
    senderIp.fromString(ip);
    if (senderIp == activeIP()) return;

    uint16_t senderChipId   = chipHex   ? (uint16_t)strtoul(chipHex, nullptr, 16) : 0;
    uint8_t  senderBatPct   = batPctStr ? (uint8_t)atoi(batPctStr) : 0;
    int8_t   senderRssi     = rssiStr   ? (int8_t)atoi(rssiStr)    : 0;
    uint8_t  senderSyncEnabled = syncEnabledStr ? (uint8_t)atoi(syncEnabledStr) : 0;
    uint16_t senderSyncMask = syncMaskStr ? (uint16_t)strtoul(syncMaskStr, nullptr, 10) : 0;
    if (syncEnabledStr && !syncMaskStr) {
        senderSyncMask = (uint16_t)strtoul(syncEnabledStr, nullptr, 10);
        senderSyncEnabled = senderSyncMask ? 1 : 0;
    }
    uint16_t myChipId     = (uint16_t)ESP.getEfuseMac();

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

    // Aktualizovat existující peer nebo přidat nový
    for (int i = 0; i < _peerCount; i++) {
        if (strcmp(_peers[i].hostname, host) == 0) {
            _peers[i].ip.fromString(ip);
            _peers[i].lastSeenMs  = millis();
            _peers[i].batPct      = senderBatPct;
            _peers[i].rssi        = senderRssi;
            _peers[i].syncEnabled = senderSyncEnabled;
            _peers[i].syncMask    = senderSyncMask;
            return;
        }
    }
    if (_peerCount < MAX_PEERS) {
        strlcpy(_peers[_peerCount].hostname, host, sizeof(_peers[_peerCount].hostname));
        _peers[_peerCount].ip.fromString(ip);
        _peers[_peerCount].lastSeenMs  = millis();
        _peers[_peerCount].batPct      = senderBatPct;
        _peers[_peerCount].rssi        = senderRssi;
        _peers[_peerCount].syncEnabled = senderSyncEnabled;
        _peers[_peerCount].syncMask    = senderSyncMask;
        _peerCount++;
        LOG("[discovery] peer: %s (%s)\n", host, ip);
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

void WifiControl::handlePeers() {
    String json = "[";
    for (int i = 0; i < _peerCount; i++) {
        if (i > 0) json += ",";
        json += "{\"hostname\":\""    + String(_peers[i].hostname)    + "\","
              + "\"ip\":\""         + _peers[i].ip.toString()       + "\","
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
