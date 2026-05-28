#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "pix_player.h"
#include "effect_player.h"
#include "led_driver.h"

// ESP-NOW broadcast sync. Any device can trigger play/stop/effect.
class SyncControl {
public:
    SyncControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds);

    // syncMask: bit 0 = class 1, bit 9 = class 10.
    bool begin(uint16_t syncMask = 1, bool syncEnabled = true);
    void setSyncMask(uint16_t syncMask);
    void setSyncEnabled(bool enabled);
    bool isSyncActive() const { return _syncEnabled && _syncMask != 0; }

    // Call from the wifi_ctrl task loop. Processes received packets.
    void process();

    void broadcastPlay(const char* file, uint8_t endBehavior = 255, uint32_t delayMs = 200, uint8_t programSlot = 0);
    void broadcastStop();
    void broadcastEffect(const EffectParams& p);
    void broadcastBrightness(uint8_t brightness);

    static SyncControl* _instance;  // for C callback

private:
    static constexpr uint8_t CMD_PLAY   = 1;
    static constexpr uint8_t CMD_STOP   = 2;
    static constexpr uint8_t CMD_EFFECT = 3;
    static constexpr uint8_t CMD_BRIGHTNESS = 4;

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

    struct __attribute__((packed)) Packet {
        uint8_t  cmd;
        uint16_t channelMask;
        union {
            PlayData   play;
            EffectData effect;
            BrightnessData brightness;
        };
    };

    struct __attribute__((packed)) QueuedPacket {
        Packet pkt;
        int64_t rxUs;
    };

    static void recvCb(const uint8_t* mac, const uint8_t* data, int len);
    void handlePacket(const Packet& pkt, int64_t rxUs);

    PixPlayer&    _player;
    EffectPlayer& _effectPlayer;
    ILedDriver&   _leds;
    QueueHandle_t _queue    = nullptr;
    uint16_t      _syncMask = 1;
    bool          _syncEnabled = true;
};
