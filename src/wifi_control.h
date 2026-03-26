#pragma once

#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "pix_player.h"
#include "app_config.h"

class SyncControl;  // forward declaration

static constexpr uint16_t DISCOVERY_PORT   = 4210;
static constexpr int      MAX_PEERS        = 8;
static constexpr uint32_t PEER_EXPIRE_MS   = 90000;
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 30000;

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
    void handlePeers();
    void handleConfigGet();
    void handleConfigPost();

    void announce();
    void receivePeers();
    void expirePeers();

    struct Peer {
        char      hostname[32];
        IPAddress ip;
        uint32_t  lastSeenMs;
    };

    PixPlayer&   _player;
    AppConfig&   _cfg;
    SyncControl* _sync;
    WebServer    _server{80};

    File         _uploadFile;
    bool         _apMode = false;

    WiFiUDP  _udp;
    Peer     _peers[MAX_PEERS];
    int      _peerCount      = 0;
    uint32_t _lastAnnounceMs = 0;
};
