#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Audio-only extension on the existing ESP-NOW broadcast peer. No relay hops.
namespace AudioGroup {
constexpr uint32_t TimeoutMs = 750;
constexpr uint32_t IntervalMs = 17; // At most ~59 fresh group frames/s.
constexpr uint32_t NoBeat = 0xffffffffu;
enum : uint8_t { Data = 1, Stop = 2 };
struct __attribute__((packed)) Packet {
    uint8_t magic[4] = {'A','X','G','1'};
    uint8_t kind = Data;
    uint16_t mask = 0;
    uint8_t session[16] = {};
    uint32_t sequence = 0, ageMs = 0, beats = 0;
    uint32_t beatAt[4] = {NoBeat, NoBeat, NoBeat, NoBeat};
    uint8_t mode = 0, speed = 50, width = 35, decay = 50, brightness = 80, response = 0, mirror = 0;
    uint8_t colors[9] = {};
    uint8_t levels[5] = {};
    uint8_t spectrum[64] = {};
};
static_assert(sizeof(Packet) == 136, "Audio group wire size changed");
inline bool valid(const Packet& p) {
    uint8_t owner = 0;
    for (auto b : p.session) owner |= b;
    return !memcmp(p.magic, "AXG1", 4) && (p.kind == Data || p.kind == Stop) &&
        p.mask && !(p.mask & ~0x3ff) && owner && p.mode <= 9 && p.speed <= 100 &&
        p.width <= 100 && p.decay <= 100 && p.brightness <= 100 && p.response <= 3 && p.mirror <= 1;
}

// A stop/override retires the session so late data cannot turn the LEDs back on.
class Receiver {
public:
    enum Result { Rejected, Frame, Stopped };
    bool active() const { return _active; }
    uint16_t mask() const { return _packet.mask; }
    bool expired(uint32_t now) const { return _active && now - _last > TimeoutMs; }
    void expire() { _active = false; }
    void block() {
        if (_seen) retire(_packet.session, _mac);
        _active = false;
    }
    Result accept(const Packet& p, const uint8_t* mac, uint32_t now, uint32_t queuedMs,
                  bool enabled, uint16_t localMask) {
        if (!enabled || !(p.mask & localMask) || queuedMs > 100 || !valid(p)) return Rejected;
        if (expired(now)) expire();
        for (const auto& retired : _retired)
            if (!memcmp(retired, p.session, 16) && !memcmp(retired + 16, mac, 6)) return Rejected;
        if (_active && (memcmp(_mac, mac, 6) || memcmp(_packet.session, p.session, 16))) return Rejected;
        if (_seen && !memcmp(_mac, mac, 6) && !memcmp(_packet.session, p.session, 16) && ((int32_t)(p.sequence - _packet.sequence) <= 0 ||
            (int32_t)(p.ageMs - _packet.ageMs) < 0)) return Rejected;
        if (p.kind == Stop) {
            retire(p.session, mac);
            _active = false;
            return Stopped;
        }
        memcpy(_mac, mac, 6);
        _packet = p;
        _last = now;
        _active = _seen = true;
        return Frame;
    }
private:
    void retire(const uint8_t* session, const uint8_t* mac) {
        memcpy(_retired[_retiredIndex], session, 16);
        memcpy(_retired[_retiredIndex] + 16, mac, 6);
        _retiredIndex = (_retiredIndex + 1) % 8;
    }
    bool _active = false, _seen = false;
    uint32_t _last = 0;
    uint8_t _mac[6] = {}, _retired[8][22] = {}, _retiredIndex = 0;
    Packet _packet;
};
}
