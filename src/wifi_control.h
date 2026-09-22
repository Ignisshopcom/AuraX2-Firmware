#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "pix_player.h"
#include "effect_player.h"
#include "audio_stream.h"
#include "audio_group.h"
#include "app_config.h"
#include "battery.h"
#include "program_storage.h"

class SyncControl;  // forward declaration

static constexpr uint16_t DISCOVERY_PORT   = 4210;
static constexpr int      MAX_PEERS        = 32;
static constexpr uint32_t PEER_EXPIRE_MS   = 90000;
static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 10000;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 30000;
static constexpr uint32_t STA_RETRY_INITIAL_MS = 500;
static constexpr uint32_t STA_RETRY_MAX_MS = 4000;
static constexpr uint32_t STA_RETRY_WATCHDOG_MS = 8000;
static constexpr uint32_t STA_FRAMEWORK_FIRST_RETRY_GRACE_MS = 8000;

class WifiControl {
public:
    WifiControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds, AppConfig& cfg,
                ProgramStorage& programStorage, SyncControl* sync = nullptr, bool fsMounted = true);

    // Connect to WiFi and start HTTP server. Returns false on timeout.
    bool begin(uint32_t timeoutMs = STA_CONNECT_TIMEOUT_MS);

    // Call from loop() — processes pending HTTP requests
    void handle();

private:
    struct Peer;

    void handleRoot();
    void handleUpload();
    void handlePlay();
    void handleStop();
    void handleEffectStart();
    void handleEffectStop();
    void handleAudioReactiveStart();
    void receiveAudioStream();
    bool startAudioReactiveOutput();
    void processAudioGroup();
    void consumeAudioInput(const uint8_t* levels, const uint8_t* spectrum);
    void beginAudioGroup();
    void endAudioGroup();
    void updateAudioGroupSettings(const AudioReactiveSettings& settings);
    void acceptAudioGroup(const AudioGroup::Packet& packet, const uint8_t* mac, uint32_t queuedMs);
    void handleAudioReactiveData();
    void handleAudioReactiveStop();
    void handleAudioReactiveSettings();
    void handlePrograms();
    void handleProgramDownload();
    void handleProgramSelect();
    void handleProgramDelete();
    void handleProgramReorder();
    void handleProgramStart();
    void handleProgramUploadStart();
    void handleProgramUploadChunk();
    void handleProgramUploadFinish();
    void handleProgramUploadAbort();
    void handleProgramUploadStream();
    void handleIdentify();
    void handlePower();
    void handleSyncNow();
    void handleStatus();
    void handlePeers();
    void handleWledJson();
    void handleWledInfo();
    void handleWledState();
    void handleWledStatePost();
    void handleWledConfig();
    void handleWledConfigPost();
    void handleWledEffects();
    void handleWledPalettes();
    void handleConfigGet();
    void handleConfigPost();
    void handleFirmwareStatus();
    void handleFirmwareCheck();
    void handleRescue();
    void handleOta();
    void handleCaptivePortal();
    void sendCorsHeaders();
    void handleCorsOptions();

    void mdnsBegin(const char* hostname);
    IPAddress activeIP() const;
    String rootUrl() const;
    bool isIpHost(const String& host) const;
    bool shouldRedirectCaptive();
    uint8_t apClientCount() const;
    const char* staSsid() const;
    const char* staPassword() const;
    bool validateProgramForPlay(const String& path);
    bool requestAllowsRelay();
    bool shouldFanoutToPeer(const Peer& peer) const;
    void fanoutHttpGet(const char* pathAndQuery);
    void fanoutHttpPost(const char* path, const String& body);
    void sendPhotonProgramCommand(bool start, uint16_t prefix = 0);
    void fanoutStop();
    void fanoutProgramStart(uint8_t slot, int64_t startUs);
    void fanoutEffect(const EffectParams& p);
    bool connectSta(uint32_t timeoutMs);
    void enableManagedReconnect(bool enabled);
    void processManagedReconnect(uint32_t now);
    bool startSoftApRadio();
    void startFallbackAp();
    void stopFallbackAp();
    void rescueAction(uint8_t action);
    void startStaServices();
    void maintainWifi();
    void announce();
    void receivePeers();
    void expirePeers();
    void sortPeers();
    bool saveRuntimeConfig();
    void scheduleRuntimeConfigSave(uint32_t delayMs = 1200);
    void flushRuntimeConfigSave();
    void rememberSyncedProgram(const char* file, uint8_t endBehavior);
    void rememberSyncedEffect(const EffectParams& p);
    void rememberSyncedBrightness(uint8_t brightness);
    void beginRealtimeUdp();
    void stopRealtimeUdp();
    void receiveRealtimeUdp();
    bool ensureRealtimeBuffer();
    void enterRealtimeMode();
    void writeRealtimeRgb(uint32_t byteOffset, const uint8_t* rgb, uint16_t len);
    void showRealtimeBuffer();
    void handleDdpPacket(uint8_t* packet, int len);
    void handleWledRealtimePacket(uint8_t* packet, int len);
    bool storageReady();
    void stopAudioReactive(bool restorePreviousEffect, bool retireGroup = true);
    bool flushProgramUploadBatch();
    void releaseProgramUploadBatch();
    bool checkFirmwareManifest(bool force);
    String firmwareStatusJson() const;
    String wledInfoJson();
    String wledStateJson();
    String wledConfigJson();
    String wledEffectsJson();
    String wledPalettesJson();

    struct Peer {
        char      hostname[32];
        IPAddress ip;
        uint32_t  lastSeenMs;
        uint16_t  chipId;
        uint8_t   batPct;
        int8_t    rssi;
        uint8_t   syncEnabled;
        uint16_t  syncMask;
    };

    struct FirmwareCheckState {
        bool     checked = false;
        bool     updateAvailable = false;
        uint32_t checkedAtMs = 0;
        uint32_t remoteBuild = 0;
        uint32_t remoteSize = 0;
        char     remoteVersion[16] = {};
        char     remoteUrl[160] = {};
        char     remotePage[160] = {};
        char     remoteNotes[160] = {};
        char     error[128] = {};
    };

    PixPlayer&    _player;
    EffectPlayer& _effectPlayer;
    ILedDriver&   _leds;
    AppConfig&    _cfg;
    ProgramStorage& _programStorage;
    SyncControl* _sync;
    WebServer    _server{80};
    WiFiServer   _programUploadServer{4211};

    File         _uploadFile;
    String       _uploadPath;
    size_t       _uploadWritten = 0;
    size_t       _uploadMaxBytes = 0;
    size_t       _uploadExpectedBytes = 0;
    size_t       _uploadCommittedBytes = 0;
    uint8_t*     _uploadBatch = nullptr;
    size_t       _uploadBatchLength = 0;
    size_t       _uploadBatchCapacity = 0;
    bool         _uploadError = false;
    int          _uploadErrorStatus = 500;
    String       _uploadErrorMessage;
    bool         _uploadEscaped = false;
    bool         _uploadEscapePending = false;
    String       _chunkUploadPath;
    String       _chunkUploadFinalPath;
    size_t       _chunkUploadExpected = 0;
    size_t       _chunkUploadWritten = 0;
    size_t       _chunkUploadCommitted = 0;
    File         _chunkUploadFile;
    bool         _chunkUploadActive = false;
    bool         _apMode = false;
    bool         _apActive = false;
    bool         _apHadClient = false;
    bool         _staServicesStarted = false;
    uint8_t      _fallbackApChannel = 1;
    bool         _fsMounted = true;
    uint32_t     _lastStaRetryMs = 0;
    uint32_t     _staDisconnectedSinceMs = 0;
    uint8_t      _staRetryCount = 0;
    bool         _runtimeSavePending = false;
    uint32_t     _runtimeSaveAtMs = 0;
    bool         _audioReactiveActive = false;
    bool         _audioResumeEffect = false;
    uint32_t     _audioLastPacketMs = 0;
    String       _audioSession;
    WiFiUDP      _audioUdp;
    bool         _audioUdpStarted = false;
    IPAddress    _audioOwnerIp;
    AudioStream::Receiver _audioReceiver;
    uint32_t     _audioLastAckMs = 0;
    AudioReactiveSettings _audioSettings;
    AudioGroup::Receiver _groupReceiver;
    AudioGroup::Packet _groupPacket, _groupStop;
    bool _groupMaster = false, _groupFollower = false, _groupPending = false;
    bool _groupMayResume = false;
    uint32_t _groupResumeRevision = 0;
    uint8_t _groupStopRepeats = 0, _groupPrevious = 0;
    uint32_t _groupStarted = 0, _groupSent = 0, _groupStopSent = 0, _groupLastBeat = 0;
    uint32_t _groupFramesSent = 0;

    DNSServer      _dns;
    WiFiUDP        _udp;
    WiFiUDP        _wledRealtimeUdp;
    WiFiUDP        _ddpUdp;
    bool           _realtimeUdpStarted = false;
    bool           _realtimeActive = false;
    uint32_t       _lastRealtimeMs = 0;
    uint8_t*       _realtimeBuf = nullptr;
    uint16_t       _realtimeBufLeds = 0;
    Peer           _peers[MAX_PEERS];
    int            _peerCount      = 0;
    uint32_t       _lastAnnounceMs = 0;
    char           _wantedHostname[32] = {};  // hostname z configu; po konfliktu zkusíme znovu jakmile peer zmizí
    FirmwareCheckState _fwCheck;
    BatteryMonitor _batMonitor;
};
