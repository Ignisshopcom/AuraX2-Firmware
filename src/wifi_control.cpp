#include "wifi_control.h"
#include "sync_control.h"
#include "battery.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ── HTML UI ───────────────────────────────────────────────────────────────────

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AuraX</title>
<style>
  body { font-family: sans-serif; max-width: 500px; margin: 40px auto; padding: 0 16px; }
  h1 { font-size: 1.4rem; }
  button { padding: 10px 20px; margin: 4px; font-size: 1rem; cursor: pointer; border: none; border-radius: 4px; }
  .play { background:#2a2; color:#fff; }
  .stop { background:#a22; color:#fff; }
  .save { background:#24a; color:#fff; }
  .reboot { background:#a60; color:#fff; }
  .upload-area { border: 2px dashed #aaa; padding: 20px; text-align: center; border-radius: 8px; margin: 16px 0; }
  details { margin-top: 16px; }
  summary { cursor: pointer; font-weight: bold; padding: 8px 0; }
  .cfg-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 12px 0; }
  .cfg-grid label { display: flex; flex-direction: column; font-size: 0.85rem; }
  .cfg-grid input, .cfg-grid select { padding: 4px; margin-top: 2px; font-size: 1rem; }
  #status, #cfgMsg, #upProg { font-size: 0.9rem; color: #555; margin-top: 8px; }
</style>
</head><body>
<h1>AuraX</h1>

<div>
  <button class="play" onclick="fetch('/play').then(refresh)">&#9654; Play</button>
  <button class="stop" onclick="fetch('/stop').then(refresh)">&#9632; Stop</button>
  <button class="stop" onclick="fetch('/off').then(refresh)">&#9866; Off</button>
</div>

<div class="upload-area">
  <p>Nahrát .pix soubor</p>
  <input type="file" id="file" accept=".pix">
  <br><br>
  <button onclick="upload()">Nahrát</button>
  <p id="upProg"></p>
</div>

<div id="status">načítám...</div>

<details>
  <summary>&#9881; Nastavení</summary>
  <form id="cfg">
  <div class="cfg-grid">
    <label>LED typ
      <select name="ledType">
        <option value="0">APA102</option>
        <option value="1">WS281x</option>
      </select>
    </label>
    <label>Počet LED <input type="number" name="numLeds" min="1" max="1000"></label>
    <label>Data pin <input type="number" name="dataPin" min="0" max="48"></label>
    <label>CLK pin <input type="number" name="clkPin" min="0" max="48"></label>
    <label style="grid-column:1/-1">PIX soubor <input type="text" name="pixFile" style="width:100%;box-sizing:border-box"></label>
    <label>WiFi SSID <input type="text" name="ssid"></label>
    <label>WiFi heslo <input type="password" name="password"></label>
  </div>
  <button type="button" class="save" onclick="saveCfg()">Uložit</button>
  <button type="button" class="reboot" onclick="reboot()">Reboot</button>
  <p id="cfgMsg"></p>
  </form>
</details>

<script>
function refresh() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('status').innerHTML =
      'Stav: <b>'+(d.playing?'přehrává':'zastaveno')+'</b>'
      +(d.file?' &nbsp;|&nbsp; '+d.file:'')
      +(d.commands?' &nbsp;|&nbsp; příkazy: '+d.commands:'')
      +'<br>'+(d.ap_mode?'&#128246; AP: ':'IP: ')+d.ip
      +(d.ap_mode?' <span style="color:#a60">(bez WiFi — přímé připojení)</span>':'')
      +' &nbsp;|&nbsp; &#128267; '+d.battery_pct+'% ('+d.battery_mv+' mV)';
  });
}
function upload() {
  const f = document.getElementById('file').files[0];
  if (!f) return alert('Vyber soubor');
  const p = document.getElementById('upProg');
  p.textContent = 'Nahrávám...';
  const fd = new FormData(); fd.append('file', f, f.name);
  fetch('/upload',{method:'POST',body:fd})
    .then(r=>r.text()).then(t=>{p.textContent=t;refresh();})
    .catch(()=>{p.textContent='Chyba nahrávání';});
}
function loadCfg() {
  fetch('/config').then(r=>r.json()).then(d=>{
    const f = document.getElementById('cfg');
    Object.keys(d).forEach(k=>{ if(f[k]) f[k].value=d[k]; });
  });
}
function saveCfg() {
  const f = document.getElementById('cfg');
  const d = {
    ledType: parseInt(f.ledType.value), numLeds: parseInt(f.numLeds.value),
    dataPin: parseInt(f.dataPin.value), clkPin:  parseInt(f.clkPin.value),
    pixFile: f.pixFile.value, ssid: f.ssid.value, password: f.password.value
  };
  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
    .then(r=>r.text()).then(t=>{document.getElementById('cfgMsg').textContent=t;});
}
function reboot() {
  fetch('/reboot',{method:'POST'});
  document.getElementById('cfgMsg').textContent='Rebootuji...';
}
setInterval(refresh, 2000);
refresh(); loadCfg();
</script>
</body></html>
)html";

// ── WifiControl ───────────────────────────────────────────────────────────────

WifiControl::WifiControl(PixPlayer& player, AppConfig& cfg, SyncControl* sync)
    : _player(player), _cfg(cfg), _sync(sync) {}

bool WifiControl::begin(uint32_t timeoutMs) {
    if (strlen(_cfg.ssid) == 0) {
        _apMode = true;
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(_cfg.ssid, _cfg.password);
        Serial.printf("[wifi] connecting to %s", _cfg.ssid);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeoutMs) {
                Serial.println("\n[wifi] timeout, starting AP");
                _apMode = true;
                break;
            }
            delay(250);
            Serial.print('.');
        }
        if (!_apMode)
            Serial.printf("\n[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
    }

    if (_apMode) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        char apSsid[32];
        snprintf(apSsid, sizeof(apSsid), "AuraX-%04X", (uint16_t)ESP.getEfuseMac());
        WiFi.softAP(apSsid);
        Serial.printf("[wifi] AP mode: SSID=%s IP=%s\n", apSsid, WiFi.softAPIP().toString().c_str());
    }

    _server.on("/",       HTTP_GET,  [this]() { handleRoot();      });
    _server.on("/play",   HTTP_GET,  [this]() { handlePlay();      });
    _server.on("/stop",   HTTP_GET,  [this]() { handleStop();      });
    _server.on("/off",    HTTP_GET,  [this]() {
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
            _server.send(200, "text/plain", "OK — soubor nahrán jako " + String(_cfg.pixFile));
        },
        [this]() {
            HTTPUpload& up = _server.upload();
            if (up.status == UPLOAD_FILE_START) {
                _player.unload();
                if (LittleFS.exists(_cfg.pixFile)) LittleFS.remove(_cfg.pixFile);
                _uploadFile = LittleFS.open(_cfg.pixFile, "w");
                if (!_uploadFile) { Serial.println("[upload] open failed"); return; }
                Serial.printf("[upload] start: %s\n", up.filename.c_str());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (_uploadFile) _uploadFile.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                if (_uploadFile) { _uploadFile.close(); Serial.printf("[upload] done: %u bytes\n", up.totalSize); }
            }
        }
    );

    _server.begin();
    return true;
}

void WifiControl::handle() {
    _server.handleClient();
}

// ── handlers ──────────────────────────────────────────────────────────────────

void WifiControl::handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WifiControl::handlePlay() {
    if (_sync) {
        _sync->broadcastPlay(_cfg.pixFile);
    } else {
        _player.stopTask();
        int err = _player.load(_cfg.pixFile);
        if (err) { _server.send(500, "text/plain", "load failed: " + String(err)); return; }
        _player.startTask(1);
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStop() {
    if (_sync) {
        _sync->broadcastStop();
    } else {
        _player.stopTask();
        _player.unload();
    }
    _server.send(200, "text/plain", "OK");
}

void WifiControl::handleStatus() {
    uint16_t mv  = batteryMillivolts();
    uint8_t  pct = batteryPercent(mv);
    String json = "{";
    json += "\"playing\":"     + String(_player.isLoaded() ? "true" : "false") + ",";
    json += "\"commands\":"    + String(_player.numCommands()) + ",";
    json += "\"file\":\""      + String(LittleFS.exists(_cfg.pixFile) ? _cfg.pixFile : "") + "\",";
    json += "\"ip\":\""        + (_apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",";
    json += "\"ap_mode\":"     + String(_apMode ? "true" : "false") + ",";
    json += "\"battery_mv\":"  + String(mv) + ",";
    json += "\"battery_pct\":" + String(pct);
    json += "}";
    _server.send(200, "application/json", json);
}

void WifiControl::handleConfigGet() {
    StaticJsonDocument<512> doc;
    doc["ledType"]  = _cfg.ledType;
    doc["numLeds"]  = _cfg.numLeds;
    doc["dataPin"]  = _cfg.dataPin;
    doc["clkPin"]   = _cfg.clkPin;
    doc["ssid"]     = _cfg.ssid;
    doc["password"] = _cfg.password;
    doc["pixFile"]  = _cfg.pixFile;
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WifiControl::handleConfigPost() {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "text/plain", "JSON error");
        return;
    }
    _cfg.ledType = doc["ledType"] | _cfg.ledType;
    _cfg.numLeds = doc["numLeds"] | _cfg.numLeds;
    _cfg.dataPin = doc["dataPin"] | _cfg.dataPin;
    _cfg.clkPin  = doc["clkPin"]  | _cfg.clkPin;
    strlcpy(_cfg.ssid,     doc["ssid"]     | _cfg.ssid,     sizeof(_cfg.ssid));
    strlcpy(_cfg.password, doc["password"] | _cfg.password, sizeof(_cfg.password));
    strlcpy(_cfg.pixFile,  doc["pixFile"]  | _cfg.pixFile,  sizeof(_cfg.pixFile));

    if (saveConfig(_cfg)) {
        _server.send(200, "text/plain", "Uloženo — reboot pro aktivaci");
    } else {
        _server.send(500, "text/plain", "Chyba zápisu");
    }
}
