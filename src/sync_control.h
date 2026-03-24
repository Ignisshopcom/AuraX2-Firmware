#pragma once

#include <stdint.h>
#include "pix_player.h"

// ESP-NOW broadcast sync — any device can trigger play/stop,
// all peers start at the same absolute time (delayMs margin).
class SyncControl {
public:
    explicit SyncControl(PixPlayer& player);

    // Call after WiFi.mode(WIFI_STA). Returns false on failure.
    bool begin();

    // Broadcast play to all peers and schedule local start.
    void broadcastPlay(const char* file, uint32_t delayMs = 200);
    void broadcastStop();

    static SyncControl* _instance;  // for C callback

private:
    static void recvCb(const uint8_t* mac, const uint8_t* data, int len);
    void handlePacket(const uint8_t* data, int len);

    static constexpr uint8_t CMD_PLAY = 1;
    static constexpr uint8_t CMD_STOP = 2;

    struct __attribute__((packed)) Packet {
        uint8_t  cmd;
        uint32_t delayMs;
        char     file[64];
    };

    PixPlayer& _player;
};
