#include "wifi_control.h"
#include "sync_control.h"
#include "config.h"
#include <WiFi.h>

// ── minimal HTML UI ───────────────────────────────────────────────────────────

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AuraX</title>
<style>
  body { font-family: sans-serif; max-width: 480px; margin: 40px auto; padding: 0 16px; }
  h1 { font-size: 1.4rem; }
  button { padding: 10px 24px; margin: 4px; font-size: 1rem; cursor: pointer; }
  .play  { background: #2a2; color: #fff; border: none; border-radius: 4px; }
  .stop  { background: #a22; color: #fff; border: none; border-radius: 4px; }
  .upload-area { border: 2px dashed #aaa; padding: 24px; text-align: center;
                 border-radius: 8px; margin: 16px 0; }
  #status { font-size: 0.9rem; color: #555; margin-top: 16px; }
</style>
</head><body>
<h1>AuraX</h1>

<div>
  <button class="play"  onclick="fetch('/play').then(refresh)">▶ Play</button>
  <button class="stop"  onclick="fetch('/stop').then(refresh)">■ Stop</button>
</div>

<div class="upload-area">
  <p>Nahrát .pix soubor</p>
  <input type="file" id="file" accept=".pix">
  <br><br>
  <button onclick="upload()">Nahrát</button>
  <p id="progress"></p>
</div>

<div id="status">načítám...</div>

<script>
function refresh() {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('status').innerHTML =
      'Stav: <b>' + (d.playing ? 'přehrává' : 'zastaveno') + '</b>'
      + (d.file ? ' &nbsp;|&nbsp; soubor: ' + d.file : '')
      + (d.commands ? ' &nbsp;|&nbsp; příkazy: ' + d.commands : '')
      + '<br>IP: ' + d.ip;
  });
}
function upload() {
  const f = document.getElementById('file').files[0];
  if (!f) return alert('Vyber soubor');
  const p = document.getElementById('progress');
  p.textContent = 'Nahrávám...';
  const fd = new FormData();
  fd.append('file', f, f.name);
  fetch('/upload', { method: 'POST', body: fd })
    .then(r => r.text())
    .then(t => { p.textContent = t; refresh(); })
    .catch(() => { p.textContent = 'Chyba nahrávání'; });
}
setInterval(refresh, 2000);
refresh();
</script>
</body></html>
)html";

// ── WifiControl ───────────────────────────────────────────────────────────────

WifiControl::WifiControl(PixPlayer& player, const char* ssid, const char* password,
                         SyncControl* sync)
    : _player(player), _sync(sync), _ssid(ssid), _password(password) {}

bool WifiControl::begin(uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    Serial.printf("[wifi] connecting to %s", _ssid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("\n[wifi] timeout");
            return false;
        }
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\n[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());

    _server.on("/",       HTTP_GET,  [this]() { handleRoot();   });
    _server.on("/play",   HTTP_GET,  [this]() { handlePlay();   });
    _server.on("/stop",   HTTP_GET,  [this]() { handleStop();   });
    _server.on("/status", HTTP_GET,  [this]() { handleStatus(); });

    // POST /upload — two handlers: completion + chunk-by-chunk upload
    _server.on("/upload", HTTP_POST,
        [this]() {
            // Called after upload finishes
            if (_uploadFile) { _uploadFile.close(); }
            _server.send(200, "text/plain", "OK — soubor nahrán jako " PIX_FILE);
        },
        [this]() {
            // Called for each chunk of multipart data
            HTTPUpload& up = _server.upload();
            if (up.status == UPLOAD_FILE_START) {
                _player.unload();
                if (LittleFS.exists(PIX_FILE)) LittleFS.remove(PIX_FILE);
                _uploadFile = LittleFS.open(PIX_FILE, "w");
                if (!_uploadFile) { Serial.println("[upload] open failed"); return; }
                Serial.printf("[upload] start: %s\n", up.filename.c_str());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (_uploadFile) _uploadFile.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                if (_uploadFile) {
                    _uploadFile.close();
                    Serial.printf("[upload] done: %u bytes\n", up.totalSize);
                }
            }
        }
    );

    _server.begin();
    return true;
}

void WifiControl::handle() {
    _server.handleClient();
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

void WifiControl::handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WifiControl::handlePlay() {
    if (_sync) {
        _sync->broadcastPlay(PIX_FILE);
    } else {
        _player.stopTask();
        int err = _player.load(PIX_FILE);
        if (err) {
            _server.send(500, "text/plain", "load failed: " + String(err));
            return;
        }
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
    String json = "{";
    json += "\"playing\":"  + String(_player.isLoaded() ? "true" : "false") + ",";
    json += "\"commands\":" + String(_player.numCommands()) + ",";
    json += "\"file\":\""   + String(LittleFS.exists(PIX_FILE) ? PIX_FILE : "") + "\",";
    json += "\"ip\":\""     + WiFi.localIP().toString() + "\"";
    json += "}";
    _server.send(200, "application/json", json);
}
