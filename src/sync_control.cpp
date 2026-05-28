#include "sync_control.h"
#include "config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <string.h>

SyncControl* SyncControl::_instance = nullptr;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct SyncProgramEntry {
    char path[64];
    uint16_t storedSlot;
};

static uint16_t storedSlotFromPath(const String& path) {
    int start = path.startsWith("/") ? 1 : 0;
    if (path.length() < start + 4) return 0;
    char a = path.charAt(start);
    char b = path.charAt(start + 1);
    char c = path.charAt(start + 2);
    char sep = path.charAt(start + 3);
    if (a < '0' || a > '9' || b < '0' || b > '9' || c < '0' || c > '9') return 0;
    if (sep != '-' && sep != '_') return 0;
    return (uint16_t)((a - '0') * 100 + (b - '0') * 10 + (c - '0'));
}

static int collectSyncPrograms(SyncProgramEntry* entries, int maxEntries) {
    int count = 0;
    File root = LittleFS.open("/");
    if (!root) return 0;
    File file = root.openNextFile();
    while (file && count < maxEntries) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (!name.startsWith("/")) name = "/" + name;
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".pix")) {
                strlcpy(entries[count].path, name.c_str(), sizeof(entries[count].path));
                entries[count].storedSlot = storedSlotFromPath(name);
                count++;
            }
        }
        file = root.openNextFile();
    }
    root.close();

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            bool swap = false;
            if (entries[i].storedSlot && entries[j].storedSlot) {
                swap = entries[j].storedSlot < entries[i].storedSlot;
            } else if (entries[j].storedSlot && !entries[i].storedSlot) {
                swap = true;
            } else if (entries[i].storedSlot == entries[j].storedSlot) {
                swap = strcmp(entries[j].path, entries[i].path) < 0;
            }
            if (swap) {
                SyncProgramEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    return count;
}

static bool findProgramSlotPath(uint8_t slot, char* out, size_t outLen) {
    if (slot == 0 || !out || outLen == 0) return false;
    SyncProgramEntry entries[32];
    int count = collectSyncPrograms(entries, 32);
    if (slot > count) return false;
    strlcpy(out, entries[slot - 1].path, outLen);
    return true;
}

SyncControl::SyncControl(PixPlayer& player, EffectPlayer& effectPlayer, ILedDriver& leds)
    : _player(player), _effectPlayer(effectPlayer), _leds(leds) {
    _instance = this;
}

bool SyncControl::begin(uint16_t syncMask, bool syncEnabled) {
    setSyncMask(syncMask);
    setSyncEnabled(syncEnabled);
    if (!isSyncActive()) {
        LOGLN("[sync] disabled until SYNC is enabled and a class is selected");
    }
    _queue = xQueueCreate(4, sizeof(QueuedPacket));
    if (!_queue) {
        LOGLN("[sync] queue alloc failed");
        return false;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() != ESP_OK) {
        LOGLN("[sync] esp_now_init failed");
        return false;
    }
    esp_now_register_recv_cb(recvCb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    uint8_t primaryChannel = WiFi.channel();
    wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primaryChannel, &secondChannel);
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    peer.channel = primaryChannel;
    peer.ifidx   = (mode == WIFI_MODE_AP || (mode == WIFI_MODE_APSTA && WiFi.status() != WL_CONNECTED))
        ? WIFI_IF_AP
        : WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    LOG("[sync] wifi channel %d, if=%s, mask=0x%03x\n",
        peer.channel, peer.ifidx == WIFI_IF_AP ? "AP" : "STA", _syncMask);

    LOGLN("[sync] ESP-NOW ready");
    return true;
}

void SyncControl::setSyncMask(uint16_t syncMask) {
    _syncMask = syncMask & 0x03FF;
}

void SyncControl::setSyncEnabled(bool enabled) {
    _syncEnabled = enabled;
}

void SyncControl::recvCb(const uint8_t*, const uint8_t* data, int len) {
    if (!_instance || !_instance->_queue || len < (int)sizeof(Packet)) return;
    if (!_instance->isSyncActive()) return;
    QueuedPacket queued = {};
    memcpy(&queued.pkt, data, sizeof(Packet));
    queued.rxUs = esp_timer_get_time();
    xQueueSendFromISR(_instance->_queue, &queued, nullptr);
}

void SyncControl::process() {
    if (!_queue) return;
    QueuedPacket queued;
    if (xQueueReceive(_queue, &queued, 0) == pdTRUE) {
        handlePacket(queued.pkt, queued.rxUs);
    }
}

void SyncControl::handlePacket(const Packet& pkt, int64_t rxUs) {
    if (!isSyncActive() || (pkt.channelMask & _syncMask) == 0) {
        LOG("[sync] ignored packet (remote=0x%03x local=0x%03x)\n", pkt.channelMask, _syncMask);
        return;
    }
    if (pkt.cmd == CMD_PLAY) {
        uint32_t delayMs = pkt.play.delayMs > 30000 ? 30000 : pkt.play.delayMs;
        char slotPath[64] = {};
        const char* playFile = pkt.play.file;
        if (pkt.play.programSlot != 0) {
            if (!findProgramSlotPath(pkt.play.programSlot, slotPath, sizeof(slotPath))) {
                LOG("[sync] slot %u not found\n", pkt.play.programSlot);
                return;
            }
            playFile = slotPath;
        }
        LOG("[sync] play: %s slot=%u endBeh=%u in %u ms\n", playFile, pkt.play.programSlot, pkt.play.endBehavior, delayMs);
        int64_t startUs = rxUs + (int64_t)delayMs * 1000;
        _effectPlayer.stop();
        _player.stopTask();
        int err = _player.load(playFile);
        if (err) { LOG("[sync] load failed: %d\n", err); return; }
        _player.setEndBehavior(pkt.play.endBehavior);
        _player.scheduleStart(startUs);
        _player.startTask(1);
    } else if (pkt.cmd == CMD_STOP) {
        LOGLN("[sync] stop");
        _effectPlayer.stop();
        _player.blackout();
    } else if (pkt.cmd == CMD_EFFECT) {
        LOG("[sync] effect id=%u speed=%u\n", pkt.effect.effectId, pkt.effect.speed);
        _player.stopTask();
        _player.unload();
        EffectParams p = {};
        p.effectId    = pkt.effect.effectId;
        p.speed       = pkt.effect.speed < 10 ? 10 : (pkt.effect.speed > 1000 ? 1000 : pkt.effect.speed);
        p.intensity   = pkt.effect.intensity;
        p.dotSize     = pkt.effect.dotSize < 1 ? 1 : pkt.effect.dotSize;
        p.paletteId   = pkt.effect.paletteId;
        p.paletteSize = pkt.effect.paletteSize > 4 ? 4 : pkt.effect.paletteSize;
        p.reverse     = pkt.effect.reverse ? 1 : 0;
        for (int i = 0; i < p.paletteSize && i < 4; i++) {
            p.palette[i] = {pkt.effect.paletteR[i], pkt.effect.paletteG[i], pkt.effect.paletteB[i]};
        }
        _effectPlayer.apply(p);
    } else if (pkt.cmd == CMD_BRIGHTNESS) {
        uint8_t value = pkt.brightness.value > 100 ? 100 : pkt.brightness.value;
        LOG("[sync] brightness=%u\n", value);
        _leds.setBrightness(value);
        _player.setBrightness(value);
    }
}

void SyncControl::broadcastPlay(const char* file, uint8_t endBehavior, uint32_t delayMs, uint8_t programSlot) {
    int64_t startUs = esp_timer_get_time() + (int64_t)delayMs * 1000;
    Packet pkt = {};
    pkt.cmd              = CMD_PLAY;
    pkt.channelMask      = _syncMask;
    pkt.play.delayMs     = delayMs;
    pkt.play.endBehavior = endBehavior;
    pkt.play.programSlot = programSlot;
    strncpy(pkt.play.file, file, sizeof(pkt.play.file) - 1);

    if (isSyncActive()) {
        esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
        if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);
    }

    _effectPlayer.stop();
    _player.stopTask();
    int err = _player.load(file);
    if (err) { LOG("[sync] load failed: %d\n", err); return; }
    _player.setEndBehavior(endBehavior);
    _player.scheduleStart(startUs);
    _player.startTask(1);
}

void SyncControl::broadcastStop() {
    Packet pkt = {};
    pkt.cmd         = CMD_STOP;
    pkt.channelMask = _syncMask;

    if (isSyncActive()) {
        esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
        if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);
    }

    _effectPlayer.stop();
    _player.blackout();
}

void SyncControl::broadcastEffect(const EffectParams& p) {
    Packet pkt = {};
    pkt.cmd                = CMD_EFFECT;
    pkt.channelMask        = _syncMask;
    pkt.effect.effectId    = p.effectId;
    pkt.effect.speed       = p.speed;
    pkt.effect.intensity   = p.intensity;
    pkt.effect.dotSize     = p.dotSize;
    pkt.effect.paletteId   = p.paletteId;
    pkt.effect.paletteSize = p.paletteSize;
    pkt.effect.reverse     = p.reverse ? 1 : 0;
    for (int i = 0; i < p.paletteSize && i < 4; i++) {
        pkt.effect.paletteR[i] = p.palette[i].r;
        pkt.effect.paletteG[i] = p.palette[i].g;
        pkt.effect.paletteB[i] = p.palette[i].b;
    }

    if (isSyncActive()) {
        esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
        if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);
    }

    _player.stopTask();
    _player.unload();
    _effectPlayer.apply(p);
}

void SyncControl::broadcastBrightness(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    Packet pkt = {};
    pkt.cmd              = CMD_BRIGHTNESS;
    pkt.channelMask      = _syncMask;
    pkt.brightness.value = brightness;

    if (isSyncActive()) {
        esp_err_t r = esp_now_send(BROADCAST, (uint8_t*)&pkt, sizeof(pkt));
        if (r != ESP_OK) LOG("[sync] send failed: 0x%x\n", r);
    }

    _leds.setBrightness(brightness);
    _player.setBrightness(brightness);
}
