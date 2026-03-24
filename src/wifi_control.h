#pragma once

#include <WebServer.h>
#include <LittleFS.h>
#include "pix_player.h"
#include "app_config.h"

class SyncControl;  // forward declaration

class WifiControl {
public:
    WifiControl(PixPlayer& player, AppConfig& cfg, SyncControl* sync = nullptr);

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
    void handleConfigGet();
    void handleConfigPost();

    PixPlayer&   _player;
    AppConfig&   _cfg;
    SyncControl* _sync;
    WebServer    _server{80};

    File         _uploadFile;
};
