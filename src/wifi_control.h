#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "pix_player.h"
#include "effect_player.h"
#include "app_config.h"
#include "battery.h"

class SyncControl;  // forward declaration

static constexpr uint16_t DISCOVERY_PORT   = 4210;
static constexpr int      MAX_PEERS        = 8;
static constexpr uint32_t PEER_EXPIRE_MS   = 90000;
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 30000;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 12000;
static constexpr uint32_t STA_RETRY_INTERVAL_MS = 18000;
static constexpr uint8_t  GROUP_AP_CHANNEL = 6;

class WifiControl {
public:
    WifiControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds, AppConfig& cfg, SyncControl* sync = nullptr);

    // Connect to WiFi and start HTTP server. Returns false on timeout.
    bool begin(uint32_t timeoutMs = STA_CONNECT_TIMEOUT_MS);

    // Call from loop() — processes pending HTTP requests
    void handle();

private:
    void handleRoot();
    void handleUpload();
    void handlePlay();
    void handleStop();
    void handleEffectStart();
    void handleEffectStop();
    void handleStatus();
    void handlePeers();
    void handleConfigGet();
    void handleConfigPost();
    void handleOta();
    void handleCaptivePortal();

    void mdnsBegin(const char* hostname);
    IPAddress activeIP() const;
    String rootUrl() const;
    bool isIpHost(const String& host) const;
    bool shouldRedirectCaptive();
    uint8_t apClientCount() const;
    const char* staSsid() const;
    const char* staPassword() const;
    const char* groupPassword() const;
    bool connectSta(uint32_t timeoutMs);
    void startFallbackAp();
    void startGroupMasterAp();
    void stopFallbackAp();
    void startStaServices();
    void maintainWifi();
    void announce();
    void receivePeers();
    void expirePeers();

    struct Peer {
        char      hostname[32];
        IPAddress ip;
        uint32_t  lastSeenMs;
        uint8_t   batPct;
        int8_t    rssi;
        uint8_t   syncChannel;
    };

    PixPlayer&    _player;
    EffectPlayer& _effectPlayer;
    ILedDriver&   _leds;
    AppConfig&    _cfg;
    SyncControl* _sync;
    WebServer    _server{80};

    File         _uploadFile;
    bool         _uploadError = false;
    bool         _apMode = false;
    bool         _apActive = false;
    bool         _apHadClient = false;
    bool         _staServicesStarted = false;
    uint32_t     _lastStaRetryMs = 0;
    uint32_t     _staDisconnectedSinceMs = 0;

    DNSServer      _dns;
    WiFiUDP        _udp;
    Peer           _peers[MAX_PEERS];
    int            _peerCount      = 0;
    uint32_t       _lastAnnounceMs = 0;
    char           _wantedHostname[32] = {};  // hostname z configu; po konfliktu zkusíme znovu jakmile peer zmizí
    BatteryMonitor _batMonitor;
};
