#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <mdns.h>
#include "web_html.h"

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

String WifiControl::deviceId() const {
    uint64_t mac = ESP.getEfuseMac();
    char id[13];
    snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
    return String(id);
}

String WifiControl::locatorUrl() const {
    if (strlen(AURAX_LOCATOR_BASE_URL) == 0) return "";
    return String(AURAX_LOCATOR_BASE_URL) + "/" + deviceId();
}

bool WifiControl::connectSta(uint32_t timeoutMs) {
    _dns.stop();
    _apActive = false;
    _apMode = false;
    _apHadClient = false;
    _staServicesStarted = false;
    _lastLocatorMs = 0;
    _locatorRegistered = false;

    WiFi.disconnect(true);
    WiFi.setHostname(_cfg.hostname);
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    delay(100);
    WiFi.begin(_cfg.ssid, _cfg.password);
    WiFi.setSleep(false);
    WiFi.setHostname(_cfg.hostname);
    _lastStaRetryMs = millis();
    _staDisconnectedSinceMs = _lastStaRetryMs;
    LOG("[wifi] connecting to %s", _cfg.ssid);

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
    WiFi.mode(strlen(_cfg.ssid) ? WIFI_AP_STA : WIFI_AP);
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

    if (strlen(_cfg.ssid)) {
        WiFi.begin(_cfg.ssid, _cfg.password);
        _lastStaRetryMs = millis();
        LOGLN("[wifi] background STA retry enabled until AP client connects");
    }
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
    if (_staServicesStarted || WiFi.status() != WL_CONNECTED) return;
    MDNS.end();
    mdnsBegin(_cfg.hostname);
    NBNS.begin(_cfg.hostname);
    _udp.begin(DISCOVERY_PORT);
    announce();
    ArduinoOTA.setHostname(_cfg.hostname);
    ArduinoOTA.begin();
    _staServicesStarted = true;
    LOG("[ota] ArduinoOTA ready\n");
    registerLocator(true);
}

void WifiControl::registerLocator(bool force) {
    if (strlen(AURAX_LOCATOR_ENDPOINT) == 0) return;
    if (_apMode || WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();
    uint32_t interval = _locatorRegistered ? LOCATOR_REGISTER_INTERVAL_MS : LOCATOR_RETRY_INTERVAL_MS;
    if (!force && now - _lastLocatorMs < interval) return;
    _lastLocatorMs = now;

    StaticJsonDocument<256> doc;
    doc["id"]       = deviceId();
    doc["hostname"] = _cfg.hostname;
    doc["localIp"]  = WiFi.localIP().toString();
    doc["fw"]       = "aurax2";
    String body;
    serializeJson(doc, body);

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String endpoint = AURAX_LOCATOR_ENDPOINT;
    bool ok = false;
    if (endpoint.startsWith("https://")) {
        secureClient.setInsecure();
        ok = http.begin(secureClient, endpoint);
    } else {
        ok = http.begin(plainClient, endpoint);
    }
    if (!ok) {
        LOGLN("[locator] begin failed");
        return;
    }
    http.setTimeout(2500);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    http.end();
    _locatorRegistered = (code >= 200 && code < 300);
    LOG("[locator] register %s -> HTTP %d\n", locatorUrl().c_str(), code);
}

bool WifiControl::begin(uint32_t timeoutMs) {
    WiFi.persistent(false);
    WiFi.softAPdisconnect(true);

    if (strlen(_cfg.ssid) == 0) {
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
    _server.on("/status", HTTP_GET,  [this]() { handleStatus();    });
    _server.on("/config", HTTP_GET,  [this]() { handleConfigGet(); });
    _server.on("/config", HTTP_POST, [this]() { handleConfigPost(); });
    _server.on("/reboot", HTTP_POST, [this]() {
        _server.send(200, "text/plain", "OK");
        delay(200);
        esp_restart();
    });

    _server.on("/upload", HTTP_POST,
        [this]() {
            if (_uploadFile) _uploadFile.close();
            if (_uploadError) {
                LittleFS.remove(_cfg.pixFile);
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
        _player.setBrightness(_cfg.brightness);
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

    if (_apMode) {
        mdnsBegin(_cfg.hostname);
    } else {
        startStaServices();
    }

    return true;
}

void WifiControl::maintainWifi() {
    if (strlen(_cfg.ssid) == 0) return;

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
            LOG("[wifi] AP fallback retry to %s\n", _cfg.ssid);
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(_cfg.ssid, _cfg.password);
            _lastStaRetryMs = now;
        }
        return;
    }

    if (_staDisconnectedSinceMs == 0) _staDisconnectedSinceMs = now;
    if (now - _lastStaRetryMs > STA_RETRY_INTERVAL_MS) {
        LOG("[wifi] reconnecting to %s\n", _cfg.ssid);
        WiFi.begin(_cfg.ssid, _cfg.password);
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
        _player.stopTask();
        _player.unload();
        _leds.clear();
        LOG("[bat] auto-off: battery %u%% (<= %u%%)\n", _batMonitor.pct(), _cfg.batAutoOffThreshold);
    }
    if (!_apMode && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
        receivePeers();
        expirePeers();
        if (millis() - _lastAnnounceMs > ANNOUNCE_INTERVAL_MS) announce();
        registerLocator();
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
    saveConfig(_cfg);
    if (_sync) {
        _sync->broadcastPlay(_cfg.pixFile, _cfg.endBehavior);
    } else {
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
        _player.scheduleStart(esp_timer_get_time() + 20000);  // 20 ms — dost na spuštění tasku
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

void WifiControl::handleStatus() {
    auto st = _player.stats();
    String json = "{";
    json += "\"playing\":"          + String(_player.isLoaded() ? "true" : "false") + ",";
    json += "\"commands\":"         + String(_player.numCommands()) + ",";
    json += "\"file\":\""           + String(LittleFS.exists(_cfg.pixFile) ? _cfg.pixFile : "") + "\",";
    json += "\"frames_rendered\":"  + String(st.framesRendered) + ",";
    json += "\"frames_expected\":"  + String(st.framesExpected) + ",";
    json += "\"ip\":\""             + (_apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",";
    json += "\"hostname\":\""       + String(_cfg.hostname) + "\",";
    json += "\"device_id\":\""      + deviceId() + "\",";
    json += "\"locator_url\":\""    + locatorUrl() + "\",";
    json += "\"ap_mode\":"          + String(_apMode ? "true" : "false") + ",";
    json += "\"battery_mv\":"       + String(_batMonitor.mv()) + ",";
    json += "\"battery_pct\":"      + String(_batMonitor.pct()) + ",";
    json += "\"rssi\":"             + String(_apMode ? 0 : WiFi.RSSI());
    json += "}";
    _server.send(200, "application/json", json);
}

void WifiControl::handleConfigGet() {
    StaticJsonDocument<1024> doc;
    doc["ledType"]  = _cfg.ledType;
    doc["numLeds"]  = _cfg.numLeds;
    doc["dataPin"]  = _cfg.dataPin;
    doc["clkPin"]   = _cfg.clkPin;
    doc["ssid"]     = _cfg.ssid;
    doc["password"] = _cfg.password;
    doc["pixFile"]    = _cfg.pixFile;
    doc["brightness"] = _cfg.brightness;
    doc["tempo"]       = _cfg.tempo;
    doc["endBehavior"] = _cfg.endBehavior;
    doc["effectId"]      = _cfg.effectId;
    doc["effectSpeed"]   = _cfg.effectSpeed;
    doc["effectDotSize"] = _cfg.effectDotSize;
    doc["paletteSize"]   = _cfg.paletteSize;
    doc["mALimit"]  = _cfg.mALimit;
    doc["batPin"]              = _cfg.batPin;
    doc["batMultiplier"]       = _cfg.batMultiplier;
    doc["batCalibration"]      = _cfg.batCalibration;
    doc["batMinMv"]            = _cfg.batMinMv;
    doc["batMaxMv"]            = _cfg.batMaxMv;
    doc["batIntervalMs"]       = _cfg.batIntervalMs;
    doc["batAutoOff"]          = (bool)_cfg.batAutoOff;
    doc["batAutoOffThreshold"] = _cfg.batAutoOffThreshold;
    doc["syncChannel"]         = _cfg.syncChannel;
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
    p.effectId = doc["id"]      | 1;
    p.speed    = doc["speed"]   | 100;
    p.dotSize  = doc["dotSize"] | 3;
    if (p.speed < 10)   p.speed = 10;
    if (p.speed > 1000) p.speed = 1000;
    if (p.dotSize < 1)                  p.dotSize = 1;
    if (p.dotSize > _cfg.numLeds)       p.dotSize = _cfg.numLeds;
    JsonArray palette = doc["palette"];
    p.paletteSize = 0;
    for (JsonObject c : palette) {
        if (p.paletteSize >= 4) break;
        p.palette[p.paletteSize].r = c["r"] | 255;
        p.palette[p.paletteSize].g = c["g"] | 0;
        p.palette[p.paletteSize].b = c["b"] | 0;
        p.paletteSize++;
    }
    if (p.paletteSize == 0) { p.palette[0] = {255, 0, 0}; p.paletteSize = 1; }

    // Zastavit přehrávač před zápisem do LittleFS — vyhnout se souběžnému přístupu
    _player.stopTask();
    _player.unload();

    // Uložit do konfigurace
    _cfg.autoStart     = 1;
    _cfg.effectId      = p.effectId;
    _cfg.effectSpeed   = p.speed;
    _cfg.effectDotSize = p.dotSize;
    _cfg.paletteSize   = p.paletteSize;
    for (int i = 0; i < p.paletteSize; i++) {
        _cfg.paletteR[i] = p.palette[i].r;
        _cfg.paletteG[i] = p.palette[i].g;
        _cfg.paletteB[i] = p.palette[i].b;
    }
    saveConfig(_cfg);

    if (_sync) {
        _sync->broadcastEffect(p);
    } else {
        _effectPlayer.start(p);
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
    if (doc.containsKey("ssid") &&
        strcmp(doc["ssid"] | "", _cfg.ssid) != 0)
        wifiChanged = true;
    if (doc.containsKey("password") &&
        strcmp(doc["password"] | "", _cfg.password) != 0)
        wifiChanged = true;
    if (doc.containsKey("hostname") &&
        strcmp(doc["hostname"] | "", _cfg.hostname) != 0)
        wifiChanged = true;

    _cfg.ledType = doc["ledType"] | _cfg.ledType;
    {
        uint16_t n = doc["numLeds"] | _cfg.numLeds;
        if (n >= 1 && n <= 2048) _cfg.numLeds = n;
    }
    _cfg.dataPin = doc["dataPin"] | _cfg.dataPin;
    _cfg.clkPin  = doc["clkPin"]  | _cfg.clkPin;
    strlcpy(_cfg.ssid,     doc["ssid"]     | _cfg.ssid,     sizeof(_cfg.ssid));
    strlcpy(_cfg.password, doc["password"] | _cfg.password, sizeof(_cfg.password));
    strlcpy(_cfg.pixFile,  doc["pixFile"]  | _cfg.pixFile,  sizeof(_cfg.pixFile));
    strlcpy(_cfg.hostname, doc["hostname"] | _cfg.hostname, sizeof(_cfg.hostname));
    _cfg.brightness = doc["brightness"] | _cfg.brightness;
    _player.setBrightness(_cfg.brightness);
    _cfg.tempo        = doc["tempo"]        | _cfg.tempo;
    _cfg.endBehavior  = doc["endBehavior"]  | _cfg.endBehavior;
    _player.setTempo(_cfg.tempo);
    _player.setEndBehavior(_cfg.endBehavior);
    _cfg.effectId      = doc["effectId"]      | _cfg.effectId;
    _cfg.effectSpeed   = doc["effectSpeed"]   | _cfg.effectSpeed;
    _cfg.effectDotSize = doc["effectDotSize"] | _cfg.effectDotSize;
    _cfg.paletteSize   = doc["paletteSize"]   | _cfg.paletteSize;
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
    _cfg.syncChannel         = doc["syncChannel"]         | _cfg.syncChannel;
    _batMonitor.resetInterval();  // re-measure with new settings
    JsonArray pR = doc["paletteR"], pG = doc["paletteG"], pB = doc["paletteB"];
    for (int i = 0; i < 4; i++) {
        if (i < (int)pR.size()) _cfg.paletteR[i] = pR[i];
        if (i < (int)pG.size()) _cfg.paletteG[i] = pG[i];
        if (i < (int)pB.size()) _cfg.paletteB[i] = pB[i];
    }

    if (saveConfig(_cfg)) {
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
    char buf[96];
    snprintf(buf, sizeof(buf), "AURAX %s %s %04x %u %d %u",
        _cfg.hostname, WiFi.localIP().toString().c_str(), (uint16_t)ESP.getEfuseMac(),
        _batMonitor.pct(), (int)WiFi.RSSI(), (unsigned)_cfg.syncChannel);
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
    char* host     = strtok(nullptr, " ");
    char* ip       = strtok(nullptr, " ");
    char* chipHex  = strtok(nullptr, " ");
    char* batPctStr     = strtok(nullptr, " ");
    char* rssiStr       = strtok(nullptr, " ");
    char* syncChStr     = strtok(nullptr, " ");
    if (!cmd || strcmp(cmd, "AURAX") != 0 || !host || !ip) return;

    // Ignorovat vlastní broadcast — kontrola vždy podle IP, nezávisle na hostname
    IPAddress senderIp;
    senderIp.fromString(ip);
    if (senderIp == WiFi.localIP()) return;

    uint16_t senderChipId   = chipHex   ? (uint16_t)strtoul(chipHex, nullptr, 16) : 0;
    uint8_t  senderBatPct   = batPctStr ? (uint8_t)atoi(batPctStr) : 0;
    int8_t   senderRssi     = rssiStr   ? (int8_t)atoi(rssiStr)    : 0;
    uint8_t  senderSyncCh   = syncChStr ? (uint8_t)atoi(syncChStr) : 0;
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
            _peers[i].syncChannel = senderSyncCh;
            return;
        }
    }
    if (_peerCount < MAX_PEERS) {
        strlcpy(_peers[_peerCount].hostname, host, sizeof(_peers[_peerCount].hostname));
        _peers[_peerCount].ip.fromString(ip);
        _peers[_peerCount].lastSeenMs  = millis();
        _peers[_peerCount].batPct      = senderBatPct;
        _peers[_peerCount].rssi        = senderRssi;
        _peers[_peerCount].syncChannel = senderSyncCh;
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
              + "\"sync_channel\":" + String(_peers[i].syncChannel) + "}";
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
