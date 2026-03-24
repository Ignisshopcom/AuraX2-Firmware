#pragma once

#include <WebServer.h>
#include <LittleFS.h>
#include "pix_player.h"

class SyncControl;  // forward declaration

class WifiControl {
public:
    WifiControl(PixPlayer& player, const char* ssid, const char* password,
                SyncControl* sync = nullptr);

    // Connect to WiFi and start HTTP server. Returns false on timeout.
    bool begin(uint32_t timeoutMs = 10000);

    // Call from loop() — processes pending HTTP requests
    void handle();

private:
    void handleRoot();
    void handleUpload();
    void handlePlay();
    void handleStop();
    void handleStatus();

    PixPlayer&   _player;
    SyncControl* _sync;
    WebServer    _server{80};
    const char*  _ssid;
    const char*  _password;

    File         _uploadFile;
};
