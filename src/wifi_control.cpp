#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
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

<div style="margin:12px 0;display:flex;align-items:center;gap:8px">
  <span style="font-size:0.9rem;white-space:nowrap">&#9728; Jas</span>
  <input type="range" id="bri" min="0" max="100" style="flex:1" oninput="setBri(this.value)">
  <span id="briVal" style="font-size:0.9rem;min-width:72px;text-align:right">PIX soubor</span>
</div>
<div style="margin:12px 0;display:flex;align-items:center;gap:8px">
  <span style="font-size:0.9rem;white-space:nowrap">&#9654;&#9654; Tempo</span>
  <input type="number" id="tempo" min="10" max="500" value="100" style="width:72px;padding:4px;font-size:1rem" onchange="setTempo(this.value)">
  <span style="font-size:0.9rem;color:#555">%</span>
</div>

<div style="margin:12px 0;display:flex;align-items:center;gap:8px">
  <span style="font-size:0.9rem;white-space:nowrap">&#8635; Konec show</span>
  <select id="endBeh" onchange="setEndBeh(this.value)" style="padding:4px;font-size:1rem">
    <option value="255">Ze souboru</option>
    <option value="1">Loop</option>
    <option value="2">Keep</option>
    <option value="0">Off</option>
  </select>
</div>

<details id="efxPanel">
  <summary>&#10024; Efekty</summary>
  <div class="cfg-grid" style="margin-top:8px">
    <label>Efekt
      <select id="efxId">
        <option value="1">Solid</option>
        <option value="2">Android</option>
      </select>
    </label>
    <label>Rychlost %
      <input type="number" id="efxSpeed" value="100" min="10" max="1000" style="width:70px">
    </label>
    <label>&#352;&#237;&#345;ka
      <input type="number" id="efxDot" value="3" min="1" max="20" style="width:60px">
    </label>
  </div>
  <div style="margin:8px 0">
    <span style="font-size:0.85rem">Paleta:</span>
    <span id="palette"></span>
    <button onclick="addColor()" style="padding:2px 8px;font-size:1rem">+</button>
  </div>
  <button class="play" onclick="startEffect()">&#9654; Spustit efekt</button>
</details>

<div class="upload-area">
  <p>Nahr&#225;t .pix soubor</p>
  <input type="file" id="file" accept=".pix">
  <br><br>
  <button onclick="upload()">Nahrát</button>
  <p id="upProg"></p>
</div>

<div id="status">načítám...</div>
<div id="peers" style="font-size:0.9rem;margin-top:6px;color:#555"></div>

<details>
  <summary>&#9881; Nastavení</summary>
  <form id="cfg">
  <div class="cfg-grid">
    <label>LED typ
      <select name="ledType" onchange="updClk(this.value)">
        <option value="1">APA102</option>
        <option value="0">WS281x</option>
      </select>
    </label>
    <label>Počet LED <input type="number" name="numLeds" min="1" max="1000"></label>
    <label>Data pin <input type="number" name="dataPin" min="0" max="48"></label>
    <label id="clkLabel">CLK pin <input type="number" name="clkPin" min="0" max="48"></label>
    <label style="grid-column:1/-1">PIX soubor <input type="text" name="pixFile" style="width:100%;box-sizing:border-box"></label>
    <label>WiFi SSID <input type="text" name="ssid"></label>
    <label>WiFi heslo <input type="password" name="password"></label>
    <label style="grid-column:1/-1">Hostname (.local) <input type="text" name="hostname" pattern="[a-z0-9-]+" placeholder="aurax-xxxx" style="width:100%;box-sizing:border-box"></label>
    <label>Limit mA (0=off) <input type="number" id="cfgMALimit" name="mALimit" min="0" max="65000" step="100"></label>
    <label>mA per LED <input type="number" id="cfgMAPerLed" name="mAPerLed" min="1" max="200"></label>
  </div>
  <details style="margin:10px 0">
    <summary style="font-weight:normal;font-size:0.9rem">&#128267; Baterie</summary>
    <div class="cfg-grid" style="margin-top:8px">
      <label>ADC pin <input type="number" name="batPin" min="0" max="48"></label>
      <label>Multiplier <input type="number" name="batMultiplier" min="0.1" max="20" step="0.001"></label>
      <label>Kalibrace (V) <input type="number" name="batCalibration" step="0.001"></label>
      <label>Min mV <input type="number" name="batMinMv" min="0" max="5000"></label>
      <label>Max mV <input type="number" name="batMaxMv" min="0" max="5000"></label>
      <label>Kapacita (mAh) <input type="number" name="batCapacityMah" min="0" max="65000"></label>
      <label style="grid-column:1/-1">Interval m&#283;&#345;en&#237; (ms) <input type="number" name="batIntervalMs" min="1000" max="3600000" step="1000" style="width:120px"></label>
      <label style="grid-column:1/-1">Auto off pod <input type="number" name="batAutoOffThreshold" min="0" max="100" style="width:56px">% &nbsp;<label><input type="checkbox" name="batAutoOff"> povolen</label></label>
    </div>
  </details>
  <button type="button" class="save" onclick="saveCfg()">Uložit</button>
  <button type="button" class="reboot" onclick="reboot()">Reboot</button>
  <p id="cfgMsg"></p>
  <hr style="margin:12px 0;border:none;border-top:1px solid #ddd">
  <div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap">
    <span style="font-size:0.85rem">Firmware (.bin)</span>
    <input type="file" id="fw" accept=".bin" style="font-size:0.8rem;max-width:180px">
    <button type="button" class="save" onclick="updateFw()" style="padding:6px 12px;font-size:0.85rem">Nahrát</button>
    <span id="fwProg" style="font-size:0.85rem;color:#555"></span>
  </div>
  </form>
</details>

<script>
function updClk(v) {
  document.getElementById('clkLabel').style.display = (parseInt(v) === 1) ? '' : 'none';
}
function setEndBeh(v) {
  fetch('/endbehavior?v=' + v);
}
function setTempo(v) {
  v = Math.max(10, Math.min(500, parseInt(v) || 100));
  document.getElementById('tempo').value = v;
  fetch('/tempo?v=' + v);
}
function setBri(v) {
  document.getElementById('briVal').textContent = +v === 0 ? 'PIX soubor' : v + '%';
  fetch('/brightness?v=' + v);
}
function startEffect() {
  var colors=[...document.querySelectorAll('#palette input')].map(function(i){
    var h=i.value.slice(1);
    return {r:parseInt(h.slice(0,2),16),g:parseInt(h.slice(2,4),16),b:parseInt(h.slice(4,6),16)};
  });
  if(!colors.length) colors=[{r:255,g:0,b:0}];
  fetch('/effect',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id:+document.getElementById('efxId').value,
      speed:+document.getElementById('efxSpeed').value,
      dotSize:+document.getElementById('efxDot').value,
      palette:colors})});
}
function addColor() {
  if(document.querySelectorAll('#palette input').length>=4) return;
  var inp=document.createElement('input'); inp.type='color'; inp.value='#ff0000';
  inp.style='margin:2px;width:36px;height:28px;cursor:pointer';
  document.getElementById('palette').appendChild(inp);
}
function initPalette(rs,gs,bs,sz) {
  var p=document.getElementById('palette'); p.innerHTML='';
  for(var i=0;i<sz;i++){
    var inp=document.createElement('input'); inp.type='color';
    var r=rs[i]||0,g=gs[i]||0,b=bs[i]||0;
    inp.value='#'+('0'+r.toString(16)).slice(-2)+('0'+g.toString(16)).slice(-2)+('0'+b.toString(16)).slice(-2);
    inp.style='margin:2px;width:36px;height:28px;cursor:pointer';
    p.appendChild(inp);
  }
}
function rssiBar(dbm) {
  if (!dbm) return '';
  var s = dbm>-55?5:dbm>-65?4:dbm>-72?3:dbm>-80?2:1;
  return ' &nbsp;|&nbsp; &#128246; '+'█'.repeat(s)+'░'.repeat(5-s)+' ('+dbm+' dBm)';
}
function refresh() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('status').innerHTML =
      'Stav: <b>'+(d.playing?'přehrává':'zastaveno')+'</b>'
      +(d.file?' &nbsp;|&nbsp; '+d.file:'')
      +(d.commands?' &nbsp;|&nbsp; příkazy: '+d.commands:'')
      +(d.frames_expected?' &nbsp;|&nbsp; snímky: '+d.frames_rendered+'/'+d.frames_expected+(d.frames_expected>d.frames_rendered?' ⚠':''):'')
      +'<br>'+(d.ap_mode?'&#128246; AP: ':'IP: ')+'<a href="http://'+d.ip+'">'+d.ip+'</a>'
      +(d.hostname?' &nbsp;|&nbsp; '+d.hostname+'.local':'')
      +(d.ap_mode?' <span style="color:#a60">(Windows: použij IP odkaz)</span>':'')
      +' &nbsp;|&nbsp; &#128267; '+d.battery_pct+'% ('+d.battery_mv+' mV)'
      +(!d.ap_mode?rssiBar(d.rssi):'');
  });
  fetch('/peers').then(r=>r.json()).then(ps=>{
    document.getElementById('peers').innerHTML = ps.length
      ? '&#128279; ' + ps.map(p=>`<a href="http://${p.ip}">${p.hostname}</a> &#128267;${p.bat_pct}%${rssiBar(p.rssi)}`).join(' &nbsp;&middot;&nbsp; ')
      : '';
  }).catch(()=>{});
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
    updClk(d.ledType);
    const bri = d.brightness || 0;
    document.getElementById('bri').value = bri;
    setBri(bri);
    const t = d.tempo || 100;
    document.getElementById('tempo').value = t;
    document.getElementById('endBeh').value = (d.endBehavior !== undefined) ? d.endBehavior : 255;
    document.getElementById('efxId').value    = d.effectId    || 1;
    document.getElementById('efxSpeed').value = d.effectSpeed || 100;
    document.getElementById('efxDot').value   = d.effectDotSize || 3;
    initPalette(d.paletteR||[255],d.paletteG||[0],d.paletteB||[0],d.paletteSize||1);
    if (d.batAutoOff !== undefined) f.batAutoOff.checked = !!d.batAutoOff;
  });
}
function saveCfg() {
  const f = document.getElementById('cfg');
  var pal=[...document.querySelectorAll('#palette input')].map(function(i){
    var h=i.value.slice(1);
    return {r:parseInt(h.slice(0,2),16),g:parseInt(h.slice(2,4),16),b:parseInt(h.slice(4,6),16)};
  });
  if(!pal.length) pal=[{r:255,g:0,b:0}];
  const d = {
    ledType: parseInt(f.ledType.value), numLeds: parseInt(f.numLeds.value),
    dataPin: parseInt(f.dataPin.value), clkPin:  parseInt(f.clkPin.value),
    pixFile: f.pixFile.value, ssid: f.ssid.value, password: f.password.value,
    hostname: f.hostname.value,
    brightness:   parseInt(document.getElementById('bri').value),
    tempo:        parseInt(document.getElementById('tempo').value),
    endBehavior:  parseInt(document.getElementById('endBeh').value),
    effectId:      parseInt(document.getElementById('efxId').value),
    effectSpeed:   parseInt(document.getElementById('efxSpeed').value),
    effectDotSize: parseInt(document.getElementById('efxDot').value),
    mALimit:  parseInt(f.mALimit.value)  || 0,
    mAPerLed: parseInt(f.mAPerLed.value) || 60,
    batPin:              parseInt(f.batPin.value)              || 2,
    batMultiplier:       parseFloat(f.batMultiplier.value)     || 2.0,
    batCalibration:      parseFloat(f.batCalibration.value)    || 0.0,
    batMinMv:            parseInt(f.batMinMv.value)            || 3200,
    batMaxMv:            parseInt(f.batMaxMv.value)            || 4200,
    batCapacityMah:      parseInt(f.batCapacityMah.value)      || 0,
    batIntervalMs:       parseInt(f.batIntervalMs.value)       || 30000,
    batAutoOff:          f.batAutoOff.checked,
    batAutoOffThreshold: parseInt(f.batAutoOffThreshold.value) || 10,
    paletteSize: pal.length,
    paletteR: pal.map(function(c){return c.r;}),
    paletteG: pal.map(function(c){return c.g;}),
    paletteB: pal.map(function(c){return c.b;})
  };
  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
    .then(r=>r.text()).then(t=>{document.getElementById('cfgMsg').textContent=t;});
}
function reboot() {
  fetch('/reboot',{method:'POST'});
  document.getElementById('cfgMsg').textContent='Rebootuji...';
}
function updateFw() {
  const f = document.getElementById('fw').files[0];
  if (!f) return alert('Vyber .bin soubor');
  const p = document.getElementById('fwProg');
  p.textContent = 'Nahrávám firmware...';
  const fd = new FormData(); fd.append('firmware', f, f.name);
  fetch('/update',{method:'POST',body:fd})
    .then(r=>r.text()).then(t=>{p.textContent=t;})
    .catch(()=>{p.textContent='Chyba nahrávání';});
}
setInterval(refresh, 2000);
refresh(); loadCfg();
</script>
</body></html>
)html";

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

    _server.on("/",       HTTP_GET,  [this]() { handleRoot();      });
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
        if (v < 0 || v > 2) v = 255;  // anything out of range → from file
        _cfg.endBehavior = (uint8_t)v;
        _player.setEndBehavior(_cfg.endBehavior);
        _server.send(200, "text/plain", "OK");
    });
    _server.on("/tempo", HTTP_GET, [this]() {
        int v = _server.arg("v").toInt();
        if (v < 1)   v = 1;
        if (v > 1000) v = 1000;
        _cfg.tempo = (uint16_t)v;
        _player.setTempo(_cfg.tempo);
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
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WifiControl::handlePlay() {
    _effectPlayer.stop();
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
    doc["mAPerLed"] = _cfg.mAPerLed;
    doc["batPin"]              = _cfg.batPin;
    doc["batMultiplier"]       = _cfg.batMultiplier;
    doc["batCalibration"]      = _cfg.batCalibration;
    doc["batMinMv"]            = _cfg.batMinMv;
    doc["batMaxMv"]            = _cfg.batMaxMv;
    doc["batCapacityMah"]      = _cfg.batCapacityMah;
    doc["batIntervalMs"]       = _cfg.batIntervalMs;
    doc["batAutoOff"]          = (bool)_cfg.batAutoOff;
    doc["batAutoOffThreshold"] = _cfg.batAutoOffThreshold;
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
    _cfg.numLeds = doc["numLeds"] | _cfg.numLeds;
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
    _cfg.mAPerLed = doc["mAPerLed"] | _cfg.mAPerLed;
    _leds.setCurrentLimit(_cfg.mALimit, _cfg.mAPerLed);
    _cfg.batPin              = doc["batPin"]              | _cfg.batPin;
    _cfg.batMultiplier       = doc["batMultiplier"]       | _cfg.batMultiplier;
    _cfg.batCalibration      = doc["batCalibration"]      | _cfg.batCalibration;
    _cfg.batMinMv            = doc["batMinMv"]            | _cfg.batMinMv;
    _cfg.batMaxMv            = doc["batMaxMv"]            | _cfg.batMaxMv;
    _cfg.batCapacityMah      = doc["batCapacityMah"]      | _cfg.batCapacityMah;
    _cfg.batIntervalMs       = doc["batIntervalMs"]       | _cfg.batIntervalMs;
    if (doc.containsKey("batAutoOff")) _cfg.batAutoOff = doc["batAutoOff"] ? 1 : 0;
    _cfg.batAutoOffThreshold = doc["batAutoOffThreshold"] | _cfg.batAutoOffThreshold;
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
    snprintf(buf, sizeof(buf), "AURAX %s %s %04x %u %d",
        _cfg.hostname, WiFi.localIP().toString().c_str(), (uint16_t)ESP.getEfuseMac(),
        _batMonitor.pct(), (int)WiFi.RSSI());
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
    char* batPctStr = strtok(nullptr, " ");
    char* rssiStr   = strtok(nullptr, " ");
    if (!cmd || strcmp(cmd, "AURAX") != 0 || !host || !ip) return;

    // Ignorovat vlastní broadcast — kontrola vždy podle IP, nezávisle na hostname
    IPAddress senderIp;
    senderIp.fromString(ip);
    if (senderIp == WiFi.localIP()) return;

    uint16_t senderChipId = chipHex ? (uint16_t)strtoul(chipHex, nullptr, 16) : 0;
    uint8_t  senderBatPct = batPctStr ? (uint8_t)atoi(batPctStr) : 0;
    int8_t   senderRssi   = rssiStr   ? (int8_t)atoi(rssiStr)    : 0;
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
            _peers[i].lastSeenMs = millis();
            _peers[i].batPct = senderBatPct;
            _peers[i].rssi   = senderRssi;
            return;
        }
    }
    if (_peerCount < MAX_PEERS) {
        strlcpy(_peers[_peerCount].hostname, host, sizeof(_peers[_peerCount].hostname));
        _peers[_peerCount].ip.fromString(ip);
        _peers[_peerCount].lastSeenMs = millis();
        _peers[_peerCount].batPct = senderBatPct;
        _peers[_peerCount].rssi   = senderRssi;
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
        json += "{\"hostname\":\"" + String(_peers[i].hostname) + "\","
              + "\"ip\":\""        + _peers[i].ip.toString()    + "\","
              + "\"bat_pct\":"     + String(_peers[i].batPct)   + ","
              + "\"rssi\":"        + String(_peers[i].rssi)     + "}";
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
