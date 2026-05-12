#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <esp_wifi.h>
#include "web_html.h"

// ── WifiControl ───────────────────────────────────────────────────────────────

WifiControl::WifiControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds, AppConfig& cfg, SyncControl* sync)
    : _player(player), _effectPlayer(effectPlayer), _leds(leds), _cfg(cfg), _sync(sync) {}

bool WifiControl::begin(uint32_t timeoutMs) {
    if (strlen(_cfg.ssid) == 0) {
        _apMode = true;
    } else {
        WiFi.mode(WIFI_STA);
        for (int attempt = 1; attempt <= 2 && !_apMode; attempt++) {
            WiFi.begin(_cfg.ssid, _cfg.password);
            LOG("[wifi] connecting to %s (pokus %d/2)", _cfg.ssid, attempt);
            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED) {
                if (millis() - start > timeoutMs) {
                    WiFi.disconnect(true);
                    if (attempt < 2)
                        LOGLN("\n[wifi] timeout, zkouším znovu");
                    else
                        LOGLN("\n[wifi] timeout, starting AP");
                    break;
                }
                delay(250);
                LOG("%c", '.');
            }
            if (WiFi.status() == WL_CONNECTED) {
                LOG("\n[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (attempt == 2) {
                _apMode = true;
            }
        }
    }

    if (_apMode) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        char apSsid[32];
        snprintf(apSsid, sizeof(apSsid), "AuraX-%04X", (uint16_t)ESP.getEfuseMac());
        WiFi.softAP(apSsid);
        LOG("[wifi] AP mode: SSID=%s IP=%s\n", apSsid, WiFi.softAPIP().toString().c_str());
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

    if (MDNS.begin(_cfg.hostname)) {
        MDNS.addService("http", "tcp", 80);
        LOG("[mdns] http://%s.local\n", _cfg.hostname);
    }

    if (!_apMode) {
        _udp.begin(DISCOVERY_PORT);
        announce();

        ArduinoOTA.setHostname(_cfg.hostname);
        ArduinoOTA.begin();
        LOG("[ota] ArduinoOTA ready\n");
    }

    return true;
}

void WifiControl::handle() {
    _server.handleClient();
    if (_batMonitor.update()) {
        _player.stopTask();
        _player.unload();
        _leds.clear();
        LOG("[bat] auto-off: battery %u%% (<= %u%%)\n", _batMonitor.pct(), _cfg.batAutoOffThreshold);
    }
    if (!_apMode) {
        ArduinoOTA.handle();
        receivePeers();
        expirePeers();
        if (millis() - _lastAnnounceMs > ANNOUNCE_INTERVAL_MS) announce();
    }
}

// ── handlers ──────────────────────────────────────────────────────────────────

void WifiControl::handleRoot() {
    _server.send_P(200, "text/html", CLIENT_HTML);
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
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
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
        _server.send(200, "text/plain", "Uloženo — reboot pro aktivaci");
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
        if (MDNS.begin(_cfg.hostname)) MDNS.addService("http", "tcp", 80);
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
                if (MDNS.begin(_cfg.hostname)) MDNS.addService("http", "tcp", 80);
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
