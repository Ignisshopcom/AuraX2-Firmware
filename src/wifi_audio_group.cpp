#include "wifi_control.h"
#include "sync_control.h"
#include <esp_system.h>
#include <esp_timer.h>

void WifiControl::beginAudioGroup() {
    _groupPacket = AudioGroup::Packet{};
    esp_fill_random(_groupPacket.session, sizeof(_groupPacket.session));
    _groupPacket.session[0] |= 1;
    _groupPacket.mask = _cfg.syncMask;
    _groupStarted = millis();
    _groupSent = millis() - AudioGroup::IntervalMs;
    _groupLastBeat = millis() - 200;
    _groupPrevious = 0;
    _groupFramesSent = 0;
    _groupPending = false;
    _groupMaster = true;
}

void WifiControl::updateAudioGroupSettings(const AudioReactiveSettings& s) {
    if (_groupPacket.mode != s.mode || _groupPacket.response != s.response) {
        _groupPacket.beats = 0;
        for (uint8_t i = 0; i < 4; ++i) _groupPacket.beatAt[i] = AudioGroup::NoBeat;
        _groupPrevious = 0;
    }
    _groupPacket.mode = s.mode; _groupPacket.speed = s.speed;
    _groupPacket.width = s.width; _groupPacket.decay = s.decay;
    _groupPacket.brightness = s.brightness; _groupPacket.response = s.response;
    _groupPacket.mirror = s.mirror;
    for (uint8_t i = 0; i < 3; ++i) {
        _groupPacket.colors[i*3] = s.colors[i].r;
        _groupPacket.colors[i*3+1] = s.colors[i].g;
        _groupPacket.colors[i*3+2] = s.colors[i].b;
    }
}

void WifiControl::endAudioGroup() {
    if (!_groupMaster) return;
    _groupStop = _groupPacket;
    _groupStop.kind = AudioGroup::Stop;
    ++_groupStop.sequence;
    _groupStop.ageMs = millis() - _groupStarted;
    _groupStopRepeats = 3;
    _groupStopSent = millis() - 20;
    _groupMaster = _groupPending = false;
}

void WifiControl::consumeAudioInput(const uint8_t* levels, const uint8_t* spectrum) {
    if (!_groupMaster) {
        if (spectrum) _effectPlayer.updateAudioSpectrum(levels, spectrum);
        else _effectPlayer.updateAudioReactive(levels[0], levels[1], levels[2], levels[3], levels[4]);
        return;
    }
    const uint32_t now = millis();
    const uint8_t response = _audioSettings.response;
    const uint8_t selected = levels[response ? response : 1];
    bool beat = response ? selected > 40 && selected > _groupPrevious + 25 : levels[4] != 0;
    _groupPrevious = selected;
    if (beat && now - _groupLastBeat > 140) {
        _groupLastBeat = now;
        _groupPacket.beatAt[_groupPacket.beats % 4] = now - _groupStarted;
        ++_groupPacket.beats;
    }
    for (uint8_t i = 0; i < 5; ++i)
        _groupPacket.levels[i] = _groupPending ? max(_groupPacket.levels[i], levels[i]) : levels[i];
    for (uint8_t i = 0; i < 64; ++i) {
        uint8_t value = spectrum ? spectrum[i] : levels[i < 5 ? 1 : i < 26 ? 2 : 3];
        _groupPacket.spectrum[i] = _groupPending ? max(_groupPacket.spectrum[i], value) : value;
    }
    _groupPending = true;
}

void WifiControl::acceptAudioGroup(const AudioGroup::Packet& p, const uint8_t* mac, uint32_t queuedMs) {
    if (_audioReactiveActive && !_groupFollower) return; // A local capture owns its output.
    if (_groupFollower && !_effectPlayer.isAudioReactive()) {
        stopAudioReactive(false); // Explicit program/effect/power override wins over late audio.
        return;
    }
    auto result = _groupReceiver.accept(p, mac, millis(), queuedMs, _cfg.syncEnabled, _cfg.syncMask);
    if (result == AudioGroup::Receiver::Rejected) return;
    if (result == AudioGroup::Receiver::Stopped) {
        if (_groupFollower) stopAudioReactive(true);
        return;
    }
    if (!startAudioReactiveOutput()) {
        _groupReceiver.block();
        return;
    }
    _groupMayResume = false;
    _groupFollower = _audioReactiveActive = true;
    _audioLastPacketMs = millis();
    AudioReactiveSettings s;
    s.mode = p.mode; s.speed = p.speed; s.width = p.width; s.decay = p.decay;
    s.brightness = p.brightness; s.response = p.response; s.mirror = p.mirror;
    for (uint8_t i = 0; i < 3; ++i)
        s.colors[i] = {p.colors[i*3], p.colors[i*3+1], p.colors[i*3+2]};
    _effectPlayer.setAudioSettings(s);
    _effectPlayer.updateAudioGroup(p, p.ageMs + queuedMs);
}

void WifiControl::processAudioGroup() {
    const uint32_t now = millis();
    // Recovery from packet loss must not undo a command received during that gap.
    if (_groupMayResume && (_effectPlayer.controlRevision() != _groupResumeRevision ||
        !_cfg.syncEnabled || !(_cfg.syncMask & _groupReceiver.mask()))) {
        _groupReceiver.block();
        _groupMayResume = false;
    }
    if (_groupFollower && (!_effectPlayer.isAudioReactive() || !_cfg.syncEnabled ||
        !(_cfg.syncMask & _groupReceiver.mask()))) stopAudioReactive(false);
    if (_groupFollower && _groupReceiver.expired(now)) stopAudioReactive(true, false);
    if (_groupMaster && (!_effectPlayer.isAudioReactive() || !_cfg.syncEnabled ||
        _cfg.syncMask != _groupPacket.mask)) endAudioGroup();
    if (!_sync) return;
    AudioGroup::Packet packet;
    uint8_t mac[6];
    int64_t rxUs;
    if (_sync->pollAudioGroup(packet, mac, rxUs)) {
        uint32_t queuedMs = (esp_timer_get_time() - rxUs) / 1000;
        acceptAudioGroup(packet, mac, queuedMs);
    }
    if (_groupStopRepeats && now - _groupStopSent >= 20) {
        _groupStopSent = now;
        --_groupStopRepeats;
        _sync->sendAudioGroup(_groupStop);
        return;
    }
    if (!_groupMaster || !_groupPending || now - _groupSent < AudioGroup::IntervalMs) return;
    _groupSent = now;
    _groupPending = false;
    if (now - _audioLastPacketMs > 100) return;
    ++_groupPacket.sequence;
    _groupPacket.ageMs = now - _groupStarted;
    if (_sync->sendAudioGroup(_groupPacket)) ++_groupFramesSent;
    // The sender uses the SAME group frame/timeline, not a separate faster local stream.
    _effectPlayer.updateAudioGroup(_groupPacket, _groupPacket.ageMs);
}
