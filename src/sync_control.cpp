#include "sync_control.h"
#include "config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <string.h>

SyncControl* SyncControl::_instance = nullptr;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

SyncControl::SyncControl(PixPlayer& player) : _player(player) {
    _instance = this;
}

bool SyncControl::begin() {
    _queue = xQueueCreate(4, sizeof(Packet));
    if (!_queue) {
        LOGLN("[sync] queue alloc failed");
        return false;
    }
    // Disable WiFi power save — prevents radio sleep that causes missed ESP-NOW packets
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() != ESP_OK) {
        LOGLN("[sync] esp_now_init failed");
        return false;
    }
    esp_now_register_recv_cb(recvCb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = WiFi.channel();  // must match AP channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt  = false;
    esp_now_add_peer(&peer);
    LOG("[sync] channel %d\n", peer.channel);

    LOGLN("[sync] ESP-NOW ready");
    return true;
}

// Called from ESP-NOW callback — ISR-safe, no blocking allowed.
void SyncControl::recvCb(const uint8_t* mac, const uint8_t* data, int len) {
    if (!_instance || !_instance->_queue || len < (int)sizeof(Packet)) return;
    Packet pkt;
    memcpy(&pkt, data, sizeof(Packet));
    xQueueSendFromISR(_instance->_queue, &pkt, nullptr);
}

// Called from wifi_ctrl task — safe to block, do I/O, call vTaskDelay.
void SyncControl::process() {
    Packet pkt;
    if (xQueueReceive(_queue, &pkt, 0) == pdTRUE) {
        handlePacket(pkt);
    }
}

void SyncControl::handlePacket(const Packet& pkt) {
    if (pkt.cmd == CMD_PLAY) {
        LOG("[sync] play: %s in %u ms\n", pkt.file, pkt.delayMs);
        _player.stopTask();
        int err = _player.load(pkt.file);
        if (err) { LOG("[sync] load failed: %d\n", err); return; }
        _player.scheduleStart(esp_timer_get_time() + (int64_t)pkt.delayMs * 1000);
        _player.startTask(1);
    } else if (pkt.cmd == CMD_STOP) {
        LOGLN("[sync] stop");
        _player.stopTask();
        _player.unload();
    }
}

void SyncControl::broadcastPlay(const char* file, uint32_t delayMs) {
    Packet pkt;
    pkt.cmd     = CMD_PLAY;
    pkt.delayMs = delayMs;
    strncpy(pkt.file, file, sizeof(pkt.file) - 1);
    pkt.file[sizeof(pkt.file) - 1] = '\0';

    esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
    if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);

    _player.stopTask();
    int err = _player.load(pkt.file);
    if (err) { LOG("[sync] load failed: %d\n", err); return; }
    _player.scheduleStart(esp_timer_get_time() + (int64_t)delayMs * 1000);
    _player.startTask(1);
}

void SyncControl::broadcastStop() {
    Packet pkt;
    pkt.cmd     = CMD_STOP;
    pkt.delayMs = 0;
    pkt.file[0] = '\0';
    esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
    if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);

    _player.stopTask();
    _player.unload();
}
