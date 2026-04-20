#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "pix_player.h"
#include "effect_player.h"

// ESP-NOW broadcast sync — any device can trigger play/stop/effect,
// all peers start at the same absolute time (delayMs margin).
class SyncControl {
public:
    SyncControl(PixPlayer& player, EffectPlayer& effectPlayer);

    // Call after WiFi.mode(WIFI_STA). Returns false on failure.
    bool begin();

    // Call from the wifi_ctrl task loop — processes received packets.
    void process();

    // Broadcast play to all peers and schedule local start.
    void broadcastPlay(const char* file, uint8_t endBehavior = 255, uint32_t delayMs = 200);
    void broadcastStop();
    void broadcastEffect(const EffectParams& p);

    static SyncControl* _instance;  // for C callback

private:
    static constexpr uint8_t CMD_PLAY   = 1;
    static constexpr uint8_t CMD_STOP   = 2;
    static constexpr uint8_t CMD_EFFECT = 3;

    struct __attribute__((packed)) PlayData {
        uint32_t delayMs;
        uint8_t  endBehavior;
        char     file[64];
    };

    struct __attribute__((packed)) EffectData {
        uint8_t  effectId;
        uint16_t speed;
        uint8_t  dotSize;
        uint8_t  paletteSize;
        uint8_t  paletteR[4];
        uint8_t  paletteG[4];
        uint8_t  paletteB[4];
    };

    struct __attribute__((packed)) Packet {
        uint8_t cmd;
        union {
            PlayData   play;
            EffectData effect;
        };
    };

    static void recvCb(const uint8_t* mac, const uint8_t* data, int len);
    void handlePacket(const Packet& pkt);

    PixPlayer&    _player;
    EffectPlayer& _effectPlayer;
    QueueHandle_t _queue = nullptr;
};
