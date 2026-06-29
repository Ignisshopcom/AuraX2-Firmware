#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include "pix_player.h"
#include "effect_player.h"
#include "led_driver.h"

// ESP-NOW broadcast sync. Any device can trigger play/stop/effect.
class SyncControl {
public:
    SyncControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds);

    // syncMask: bit 0 = class 1, bit 9 = class 10.
    bool begin(uint16_t syncMask = 1, bool syncEnabled = true);
    bool refreshWifiPeer();
    void setSyncMask(uint16_t syncMask);
    void setSyncEnabled(bool enabled);
    bool isSyncActive() const { return _syncEnabled && _syncMask != 0; }
    bool isReady() const { return _espNowReady; }
    uint8_t wifiChannel() const { return _peerChannel; }
    const char* wifiInterfaceName() const;

    // Call from the wifi_ctrl task loop. Processes received packets.
    void process();

    int broadcastPlay(const char* file, uint8_t endBehavior = 255, uint32_t delayMs = 200, uint8_t programSlot = 0);
    int broadcastPlayFromAge(const char* file, uint8_t endBehavior, uint32_t ageMs, uint8_t programSlot = 0);
    void broadcastStop();
    void broadcastEffect(const EffectParams& p);
    void broadcastBrightness(uint8_t brightness);
    void broadcastRescue(uint8_t action);

    using RescueHandler = void (*)(uint8_t action, void* ctx);
    using PlayStateHandler = void (*)(const char* file, uint8_t endBehavior, void* ctx);
    using EffectStateHandler = void (*)(const EffectParams& p, void* ctx);
    using BrightnessStateHandler = void (*)(uint8_t brightness, void* ctx);
    void setRescueHandler(RescueHandler handler, void* ctx) {
        _rescueHandler = handler;
        _rescueCtx = ctx;
    }
    void setStateHandlers(PlayStateHandler playHandler,
                          EffectStateHandler effectHandler,
                          BrightnessStateHandler brightnessHandler,
                          void* ctx) {
        _playStateHandler = playHandler;
        _effectStateHandler = effectHandler;
        _brightnessStateHandler = brightnessHandler;
        _stateCtx = ctx;
    }

    static SyncControl* _instance;  // for C callback

    static constexpr uint8_t RESCUE_FORCE_AP = 1;
    static constexpr uint8_t RESCUE_REBOOT   = 2;
    static constexpr uint8_t RESCUE_STA_RETRY = 3;

private:
    #if defined(ARDUINO_ARCH_ESP32C3)
    static constexpr uint8_t DEFAULT_SEND_REPEATS = 3;
    #else
    static constexpr uint8_t DEFAULT_SEND_REPEATS = 8;
    #endif

    static constexpr uint8_t CMD_PLAY   = 1;
    static constexpr uint8_t CMD_STOP   = 2;
    static constexpr uint8_t CMD_EFFECT = 3;
    static constexpr uint8_t CMD_BRIGHTNESS = 4;
    static constexpr uint8_t CMD_RESCUE = 0x70;

    struct __attribute__((packed)) PlayData {
        uint32_t delayMs;
        uint8_t  endBehavior;
        uint8_t  programSlot;
        char     file[64];
    };

    struct __attribute__((packed)) EffectData {
        uint8_t  effectId;
        uint16_t speed;
        uint8_t  intensity;
        uint8_t  dotSize;
        uint8_t  paletteId;
        uint8_t  paletteSize;
        uint8_t  reverse;
        uint8_t  paletteR[4];
        uint8_t  paletteG[4];
        uint8_t  paletteB[4];
    };

    struct __attribute__((packed)) BrightnessData {
        uint8_t value;
    };

    struct __attribute__((packed)) RescueData {
        uint8_t action;
        uint8_t magicA;
        uint8_t magicB;
        uint8_t reserved;
    };

    struct __attribute__((packed)) Packet {
        uint8_t  cmd;
        uint16_t channelMask;
        uint32_t nonce;
        union {
            PlayData   play;
            EffectData effect;
            BrightnessData brightness;
            RescueData rescue;
        };
    };

    struct __attribute__((packed)) QueuedPacket {
        Packet pkt;
        int64_t rxUs;
    };

    static void recvCb(const uint8_t* mac, const uint8_t* data, int len);
    void handlePacket(const Packet& pkt, int64_t rxUs);
    bool sendPacket(const Packet& pkt, const char* label, uint8_t repeats = DEFAULT_SEND_REPEATS);
    bool sendTimedPlayPacket(Packet& pkt, int64_t triggerUs, const char* label, uint8_t repeats = DEFAULT_SEND_REPEATS);
    uint32_t nextNonce() const;

    PixPlayer&    _player;
    EffectPlayer& _effectPlayer;
    ILedDriver&   _leds;
    QueueHandle_t _queue    = nullptr;
    uint16_t      _syncMask = 1;
    bool          _syncEnabled = true;
    bool          _espNowReady = false;
    bool          _peerConfigured = false;
    wifi_interface_t _peerIfidx = WIFI_IF_STA;
    uint8_t       _peerChannel = 0;
    uint32_t      _lastRxNonce = 0;
    RescueHandler _rescueHandler = nullptr;
    void*         _rescueCtx = nullptr;
    PlayStateHandler _playStateHandler = nullptr;
    EffectStateHandler _effectStateHandler = nullptr;
    BrightnessStateHandler _brightnessStateHandler = nullptr;
    void*         _stateCtx = nullptr;
};
