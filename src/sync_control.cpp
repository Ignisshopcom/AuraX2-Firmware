#include "sync_control.h"
#include "config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <string.h>

SyncControl* SyncControl::_instance = nullptr;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

SyncControl::SyncControl(PixPlayer& player, EffectPlayer& effectPlayer)
    : _player(player), _effectPlayer(effectPlayer) {
    _instance = this;
}

bool SyncControl::begin(uint8_t syncChannel) {
    _syncChannel = syncChannel;
    if (_syncChannel == 0) {
        LOGLN("[sync] channel 0 — sync disabled");
        return true;
    }
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
    LOG("[sync] wifi channel %d, sync channel %d\n", peer.channel, _syncChannel);

    LOGLN("[sync] ESP-NOW ready");
    return true;
}

// Called from ESP-NOW callback — ISR-safe, no blocking allowed.
void SyncControl::recvCb(const uint8_t* mac, const uint8_t* data, int len) {
    if (!_instance || !_instance->_queue || len < (int)sizeof(Packet)) return;
    if (_instance->_syncChannel == 0) return;
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
    if (pkt.channel != _syncChannel) {
        LOG("[sync] ignored packet (ch %d != %d)\n", pkt.channel, _syncChannel);
        return;
    }
    if (pkt.cmd == CMD_PLAY) {
        uint32_t delayMs = pkt.play.delayMs > 30000 ? 30000 : pkt.play.delayMs;
        LOG("[sync] play: %s endBeh=%u in %u ms\n", pkt.play.file, pkt.play.endBehavior, delayMs);
        _effectPlayer.stop();
        _player.stopTask();
        int err = _player.load(pkt.play.file);
        if (err) { LOG("[sync] load failed: %d\n", err); return; }
        _player.setEndBehavior(pkt.play.endBehavior);
        _player.scheduleStart(esp_timer_get_time() + (int64_t)delayMs * 1000);
        _player.startTask(1);
    } else if (pkt.cmd == CMD_STOP) {
        LOGLN("[sync] stop");
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
    } else if (pkt.cmd == CMD_EFFECT) {
        LOG("[sync] effect id=%u speed=%u\n", pkt.effect.effectId, pkt.effect.speed);
        _player.stopTask();
        _player.unload();
        EffectParams p = {};
        p.effectId    = pkt.effect.effectId;
        p.speed       = pkt.effect.speed   < 10   ? 10   : (pkt.effect.speed   > 1000 ? 1000 : pkt.effect.speed);
        p.dotSize     = pkt.effect.dotSize < 1    ? 1    : pkt.effect.dotSize;
        p.paletteSize = pkt.effect.paletteSize > 4 ? 4   : pkt.effect.paletteSize;
        for (int i = 0; i < p.paletteSize && i < 4; i++) {
            p.palette[i] = {pkt.effect.paletteR[i], pkt.effect.paletteG[i], pkt.effect.paletteB[i]};
        }
        _effectPlayer.start(p);
    }
}

void SyncControl::broadcastPlay(const char* file, uint8_t endBehavior, uint32_t delayMs) {
    Packet pkt = {};
    pkt.cmd              = CMD_PLAY;
    pkt.channel          = _syncChannel;
    pkt.play.delayMs     = delayMs;
    pkt.play.endBehavior = endBehavior;
    strncpy(pkt.play.file, file, sizeof(pkt.play.file) - 1);

    if (_syncChannel == 0) {
        // sync disabled — play locally only
        _effectPlayer.stop();
        _player.stopTask();
        int err = _player.load(file);
        if (err) { LOG("[sync] load failed: %d\n", err); return; }
        _player.setEndBehavior(endBehavior);
        _player.scheduleStart(esp_timer_get_time() + (int64_t)delayMs * 1000);
        _player.startTask(1);
        return;
    }
    esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
    if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);

    _effectPlayer.stop();
    _player.stopTask();
    int err = _player.load(file);
    if (err) { LOG("[sync] load failed: %d\n", err); return; }
    _player.setEndBehavior(endBehavior);
    _player.scheduleStart(esp_timer_get_time() + (int64_t)delayMs * 1000);
    _player.startTask(1);
}

void SyncControl::broadcastStop() {
    Packet pkt = {};
    pkt.cmd     = CMD_STOP;
    pkt.channel = _syncChannel;

    if (_syncChannel == 0) {
        _effectPlayer.stop();
        _player.stopTask();
        _player.unload();
        return;
    }
    esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
    if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);

    _effectPlayer.stop();
    _player.stopTask();
    _player.unload();
}

void SyncControl::broadcastEffect(const EffectParams& p) {
    Packet pkt = {};
    pkt.cmd               = CMD_EFFECT;
    pkt.channel           = _syncChannel;
    pkt.effect.effectId   = p.effectId;
    pkt.effect.speed      = p.speed;
    pkt.effect.dotSize    = p.dotSize;
    pkt.effect.paletteSize = p.paletteSize;
    for (int i = 0; i < p.paletteSize && i < 4; i++) {
        pkt.effect.paletteR[i] = p.palette[i].r;
        pkt.effect.paletteG[i] = p.palette[i].g;
        pkt.effect.paletteB[i] = p.palette[i].b;
    }

    if (_syncChannel == 0) {
        _player.stopTask();
        _player.unload();
        _effectPlayer.start(p);
        return;
    }
    esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
    if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);

    _player.stopTask();
    _player.unload();
    _effectPlayer.start(p);
}
