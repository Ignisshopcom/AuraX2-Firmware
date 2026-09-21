#include "audio_reactive_effect.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void AudioReactiveInput::update(uint8_t volume, uint8_t bass, uint8_t mid, uint8_t treble,
                                uint8_t beat) {
    portENTER_CRITICAL(&_mux);
    _frame.volume = volume;
    _frame.bass = bass;
    _frame.mid = mid;
    _frame.treble = treble;
    _frame.beat = beat;
    _frame.group = false;
    _frame.detailed = false;
    if (beat) ++_frame.beatSequence;
    ++_frame.sequence;
    portEXIT_CRITICAL(&_mux);
}

void AudioReactiveInput::updateSpectrum(const uint8_t* levels, const uint8_t* spectrum) {
    portENTER_CRITICAL(&_mux);
    _frame.volume = levels[0];
    _frame.bass = levels[1];
    _frame.mid = levels[2];
    _frame.treble = levels[3];
    _frame.beat = levels[4];
    memcpy(_frame.spectrum, spectrum, 64);
    _frame.group = false;
    _frame.detailed = true;
    if (levels[4]) ++_frame.beatSequence;
    ++_frame.sequence;
    portEXIT_CRITICAL(&_mux);
}

void AudioReactiveInput::updateGroup(const AudioGroup::Packet& p, uint32_t ageMs) {
    portENTER_CRITICAL(&_mux);
    _frame.volume = p.levels[0]; _frame.bass = p.levels[1];
    _frame.mid = p.levels[2]; _frame.treble = p.levels[3]; _frame.beat = p.levels[4];
    memcpy(_frame.spectrum, p.spectrum, 64);
    memcpy(_frame.beatAt, p.beatAt, sizeof(_frame.beatAt));
    _frame.group = _frame.detailed = true;
    _frame.groupAgeMs = ageMs;
    memcpy(&_frame.groupEpoch, p.session, sizeof(_frame.groupEpoch));
    _frame.groupLocalMs = millis();
    _frame.beatSequence = p.beats;
    ++_frame.sequence;
    portEXIT_CRITICAL(&_mux);
}

void AudioReactiveInput::clear() {
    portENTER_CRITICAL(&_mux);
    uint32_t nextSequence = _frame.sequence + 1;
    _frame = {};
    _frame.sequence = nextSequence;
    portEXIT_CRITICAL(&_mux);
}

AudioReactiveFrame AudioReactiveInput::snapshot() {
    portENTER_CRITICAL(&_mux);
    AudioReactiveFrame frame = _frame;
    portEXIT_CRITICAL(&_mux);
    return frame;
}

void AudioReactiveInput::setSettings(const AudioReactiveSettings& settings) {
    portENTER_CRITICAL(&_mux);
    _settings = settings;
    portEXIT_CRITICAL(&_mux);
}

AudioReactiveSettings AudioReactiveInput::settingsSnapshot() {
    portENTER_CRITICAL(&_mux);
    AudioReactiveSettings settings = _settings;
    portEXIT_CRITICAL(&_mux);
    return settings;
}

AudioReactiveEffect::~AudioReactiveEffect() { free(_buf); }

void AudioReactiveEffect::reset(const EffectParams&, uint16_t numLeds) {
    free(_buf);
    _numLeds = numLeds;
    _buf = (uint8_t*)calloc(numLeds, 4);
    _phase = 0;
    _lastPacketMs = millis();
    _lastUpdateMs = millis() - 7;
    _hasAudio = false;
    _wasGroup = false;
    _groupEpoch = 0;
    memset(_target, 0, sizeof(_target));
    memset(_visual, 0, sizeof(_visual));
    _flowHead = 0;
    _flowAgeMs = 0;
    _flowStep = AudioGroup::NoBeat;
    memset(_flow, 0, sizeof(_flow));
    _volume = _bass = _mid = _treble = _beatPulse = _peak = _step = 0;
    _lastSequence = _lastBeatSequence = 0;
    _mode = 255;
    _response = 255;
    memset(_spectrum, 0, sizeof(_spectrum));
    memset(_onset, 0, sizeof(_onset));
    memset(_envelope, 0, sizeof(_envelope));
    _rippleIndex = 0;
    for (auto& radius : _ripple) radius = 65535;
}

uint8_t AudioReactiveEffect::triangle8(uint16_t phase) {
    uint8_t x = (uint8_t)(phase >> 8);
    return (x & 0x80) ? (uint8_t)((255 - x) << 1) : (uint8_t)(x << 1);
}

uint8_t AudioReactiveEffect::scale8(uint8_t value, uint8_t scale) {
    return (uint8_t)(((uint16_t)value * scale + 127) / 255);
}

uint8_t AudioReactiveEffect::addSat(uint8_t a, uint8_t b) {
    uint16_t sum = (uint16_t)a + b;
    return sum > 255 ? 255 : (uint8_t)sum;
}

EffectColor AudioReactiveEffect::gradient(const AudioReactiveSettings& s, uint8_t position) {
    uint16_t t = (uint16_t)position * 2;
    uint8_t segment = t > 255 ? 1 : 0;
    uint8_t fraction = t - (segment ? 255 : 0);
    const auto& a = s.colors[segment];
    const auto& b = s.colors[segment + 1];
    return {addSat(scale8(a.r, 255 - fraction), scale8(b.r, fraction)),
            addSat(scale8(a.g, 255 - fraction), scale8(b.g, fraction)),
            addSat(scale8(a.b, 255 - fraction), scale8(b.b, fraction))};
}

static uint8_t audioNoise(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    return (uint8_t)(x >> 16);
}

void AudioReactiveEffect::update(ILedDriver& leds, const EffectParams&) {
    if (!_buf || !_numLeds) return;
    AudioReactiveFrame input = _input.snapshot();
    AudioReactiveSettings s = _input.settingsSnapshot();
    uint32_t elapsedMs = millis() - _lastUpdateMs;
    _lastUpdateMs = millis();
    if (elapsedMs < 1) elapsedMs = 1;
    if (elapsedMs > 50) elapsedMs = 50;
    const float ticks = elapsedMs * 0.06f; // Preserve timing independently of actual render FPS.
    const bool changed = _mode != s.mode || _response != s.response || _wasGroup != input.group ||
        (input.group && _groupEpoch != input.groupEpoch);
    _wasGroup = input.group;
    _groupEpoch = input.groupEpoch;
    if (changed) {
        _hasAudio = false;
        _mode = s.mode;
        _response = s.response;
        _selectedPrevious = 0;
        _selectedBeatMs = millis() - 200;
        memset(_onset, 0, sizeof(_onset));
        memset(_envelope, 0, sizeof(_envelope));
        memset(_spectrum, 0, sizeof(_spectrum));
        memset(_flow, 0, sizeof(_flow));
        _flowHead = 0;
        _flowAgeMs = 0;
        _flowStep = AudioGroup::NoBeat;
        _phase = 0;
        _beatPulse = _step = _peak = 0;
        memset(_buf, 0, (size_t)_numLeds * 4);
        for (auto& radius : _ripple) radius = 65535;
    }

    // No queued interpolation frames: follow the latest input with a short 8 ms visual filter.
    const bool newFrame = input.sequence != _lastSequence;
    if (newFrame || changed) {
        _lastSequence = input.sequence;
        if (newFrame) _lastPacketMs = millis();
        const uint8_t selected = s.response == 1 ? input.bass : s.response == 2 ? input.mid : input.treble;
        _target[0] = s.response ? selected : input.volume;
        _target[1] = s.response ? selected : input.bass;
        _target[2] = s.response ? selected : input.mid;
        _target[3] = s.response ? selected : input.treble;
        bool beat = !input.group && (s.response ? (selected > 40 && selected > _selectedPrevious + 25 &&
            millis() - _selectedBeatMs > 140) : input.beatSequence != _lastBeatSequence);
        _lastBeatSequence = input.beatSequence;
        _selectedPrevious = selected;
        for (uint8_t b = 0; b < 64; ++b) {
            uint8_t value = input.detailed ? input.spectrum[b] :
                (b < 5 ? input.bass : b < 26 ? input.mid : input.treble);
            _onset[b] = max(_onset[b], uint8_t(value > _target[b + 4] ? value - _target[b + 4] : 0));
            _target[b + 4] = value;
        }
        if (beat) {
            _selectedBeatMs = millis();
            _beatPulse = 255;
            ++_step;
            _ripple[_rippleIndex++ % 4] = 0;
        }
    }
    if ((uint32_t)(millis() - _lastPacketMs) > 120) memset(_target, 0, sizeof(_target));
    const float follow = 1 - expf(-(float)elapsedMs / 8.0f);
    for (uint8_t b = 0; b < 68; ++b) {
        _visual[b] = _hasAudio ? _visual[b] + (_target[b] - _visual[b]) * follow : _target[b];
    }
    if (newFrame) _hasAudio = true;
    _volume = (uint8_t)roundf(_visual[0]);
    _bass = (uint8_t)roundf(_visual[1]);
    _mid = (uint8_t)roundf(_visual[2]);
    _treble = (uint8_t)roundf(_visual[3]);
    for (uint8_t b = 0; b < 64; ++b) _spectrum[b] = (uint8_t)roundf(_visual[b + 4]);

    const uint8_t release = (uint8_t)roundf(255 * powf((180 + s.decay * 72 / 100) / 255.0f, ticks));
    const uint16_t width = 512 + (uint32_t)s.width * 22000 / 100;
    const uint8_t brightness = (uint16_t)s.brightness * 255 / 100;
    const uint16_t motion = (uint16_t)roundf(s.speed * 36 * ticks);
    _peak = _volume >= _peak ? _volume : fmaxf(0, _peak - (1 + s.speed / 12) * ticks);
    const uint32_t groupAge = input.groupAgeMs + (uint32_t)(millis() - input.groupLocalMs);
    if (input.group) {
        // Common stream time anchors motion and beat phase, including a late-joining receiver.
        _phase = (uint64_t)groupAge * s.speed * 108 / 100;
        _step = (uint8_t)input.beatSequence;
        uint32_t youngest = AudioGroup::NoBeat;
        for (uint8_t i = 0; i < 4; ++i) {
            uint32_t age = groupAge - input.beatAt[i];
            if (input.beatAt[i] == AudioGroup::NoBeat || (int32_t)age < 0) { _ripple[i] = 65535; continue; }
            youngest = age < youngest ? age : youngest;
            uint64_t radius = (uint64_t)age * s.speed * 216 / 100;
            _ripple[i] = radius >= 65535 ? 65535 : (uint16_t)radius;
        }
        uint8_t decay = s.mode == 8 ? 160 + (100 - s.speed) * 9 / 10 : 180 + s.decay * 72 / 100;
        _beatPulse = youngest == AudioGroup::NoBeat ? 0 :
            (uint8_t)roundf(255 * powf(decay / 255.0f, youngest * 0.06f));
    }
    const float flowPeriodMs = 60 - s.speed * 0.5f;
    if (input.group) {
        uint32_t step = (uint32_t)(groupAge / flowPeriodMs);
        if (_flowStep == AudioGroup::NoBeat) _flowStep = step;
        uint32_t count = step - _flowStep;
        if (count > 64) count = 64;
        while (count--) {
            uint8_t index = (step - count) % 64;
            _flow[index][0] = _bass; _flow[index][1] = _mid; _flow[index][2] = _treble;
        }
        _flowStep = step;
        _flowHead = step % 64;
        _flowAgeMs = groupAge - step * flowPeriodMs;
    } else {
      _flowAgeMs += elapsedMs;
      while (_flowAgeMs >= flowPeriodMs) {
        _flowAgeMs -= flowPeriodMs;
        _flowHead = (_flowHead + 1) % 64;
        _flow[_flowHead][0] = _bass;
        _flow[_flowHead][1] = _mid;
        _flow[_flowHead][2] = _treble;
      }
    }
    _flow[_flowHead][0] = _bass;
    _flow[_flowHead][1] = _mid;
    _flow[_flowHead][2] = _treble;
    // The 64 triangular mel filters cover 30..16000 Hz. Restrict spatial
    // sampling to the chosen range; every effect also uses that range's envelope.
    const uint8_t firstBand = s.response == 2 ? 5 : s.response == 3 ? 26 : 0;
    const uint8_t lastBand = s.response == 1 ? 4 : s.response == 2 ? 25 : 63;
    for (uint8_t b = 0; b < 64; ++b) {
        _envelope[b] = max(_spectrum[b], (uint8_t)((uint16_t)_envelope[b] * release / 255));
    }
    for (uint16_t i = 0; i < _numLeds; ++i) {
        uint16_t x = _numLeds > 1 ? (uint32_t)i * 65535u / (_numLeds - 1) : 32768;
        if (s.mirror) x = _numLeds > 1 ? (uint32_t)abs((int32_t)i * 2 - (_numLeds - 1)) * 65535u / (_numLeds - 1) : 0;
        EffectColor color = gradient(s, x >> 8);
        uint32_t samplePos = (uint32_t)x * (lastBand - firstBand);
        uint8_t bin = firstBand + samplePos / 65535;
        uint8_t nextBin = bin < lastBand ? bin + 1 : lastBand;
        uint8_t fraction = (samplePos % 65535) >> 8;
        uint8_t spectral = addSat(scale8(_spectrum[bin], 255 - fraction), scale8(_spectrum[nextBin], fraction));
        uint8_t onset = max(_onset[bin], _onset[nextBin]);
        uint8_t envelope = max(_envelope[bin], _envelope[nextBin]);
        uint8_t level = 0;
        switch (s.mode) {
            case 0: { // Frequency position, spectral envelope and transient accents.
                color = gradient(s, (uint8_t)((x + _phase) >> 8));
                uint8_t shape = addSat(scale8(spectral, 255 - s.width * 2), scale8(envelope, s.width * 2));
                level = addSat(shape, onset / 2);
                break;
            }
            case 1: { // VU with a falling peak marker.
                float pos = x * 255.0f / 65535;
                float coverage = (_volume - pos) * _numLeds / 255.0f + 0.5f;
                level = _volume == 255 ? 255 : _volume == 0 ? 0 :
                    (uint8_t)(255 * fmaxf(0, fminf(1, coverage)));
                if (_peak > 0 && fabsf(pos - _peak) <= 1 + s.width / 25) level = 255;
                break;
            }
            case 2: { // Bass expands a solid pulse from the center.
                uint16_t distance = abs((int32_t)x - 32768);
                uint16_t extent = ((uint32_t)width * _bass / 255) + (uint32_t)_bass * 42;
                uint16_t feather = 300 + width / 4;
                uint32_t margin = extent > distance ? extent - distance : 0;
                uint8_t edge = margin >= feather ? 255 : margin * 255 / feather;
                level = scale8(addSat(_bass, _beatPulse / 3), edge);
                color = gradient(s, (uint8_t)(_phase >> 8));
                break;
            }
            case 3: { // Four fixed-size beat rings; no particle allocations.
                uint16_t distance = x > 32767 ? (uint32_t)x * 2 - 65535 : 65535 - (uint32_t)x * 2;
                for (auto radius : _ripple) {
                    if (radius == 65535) continue;
                    uint32_t delta = abs((int32_t)distance - radius);
                    if (delta < width) {
                        uint8_t fade = 255 - ((uint32_t)radius * (230 - s.decay * 2) / 65535);
                        level = addSat(level, scale8((uint32_t)(width - delta) * 255 / width, fade));
                    }
                }
                level = scale8(level, addSat(_volume, _beatPulse));
                // Quiet notes also feed the traveling wave; beats add rings, not an on/off gate.
                uint8_t wave = triangle8((uint32_t)distance * (2 + s.width / 20) - _phase);
                level = addSat(level, scale8(_volume, wave) / 3);
                break;
            }
            case 4: { // Spectral energy, slower envelope and local onsets in three colors.
                uint8_t a = spectral;
                uint8_t b = scale8(envelope, triangle8(x * (1 + s.width / 12) - _phase));
                uint8_t c = addSat(onset, scale8(spectral, triangle8(x + _phase)) / 3);
                color = {addSat(addSat(scale8(s.colors[0].r, a), scale8(s.colors[1].r, b)), scale8(s.colors[2].r, c)),
                         addSat(addSat(scale8(s.colors[0].g, a), scale8(s.colors[1].g, b)), scale8(s.colors[2].g, c)),
                         addSat(addSat(scale8(s.colors[0].b, a), scale8(s.colors[1].b, b)), scale8(s.colors[2].b, c))};
                level = 255;
                break;
            }
            case 5: { // Treble sparkles with local tails stored in the existing output buffer.
                uint16_t cell = (uint32_t)x * (8 + (100 - s.width) * 2) >> 16;
                uint8_t noise = audioNoise(cell + (uint32_t)(_phase >> 10) * 1987);
                level = spectral && noise < (uint16_t)addSat(spectral / 2, onset) * (10 + s.speed) / 140 ? addSat(spectral, onset) : 0;
                break;
            }
            case 6: { // Traveling bass comet.
                uint16_t behind = _phase - x;
                level = behind < width ? scale8(_bass, (uint32_t)(width - behind) * 255 / width) : 0;
                level = addSat(level, scale8(_beatPulse, behind < width / 4 ? 120 : 0));
                break;
            }
            case 7: { // Rolling music history: lows, mids and highs paint a continuous trail.
                float age = x * (8 + s.width * 55 / 100) / 65535.0f - _flowAgeMs / flowPeriodMs;
                if (age < 0) age = 0;
                uint8_t index = (uint8_t)age;
                uint8_t mix = (uint8_t)((age - index) * 255);
                uint8_t nowIndex = (_flowHead + 64 - index) % 64;
                uint8_t oldIndex = (nowIndex + 63) % 64;
                uint8_t value[3];
                for (uint8_t c = 0; c < 3; ++c)
                    value[c] = addSat(scale8(_flow[nowIndex][c], 255 - mix), scale8(_flow[oldIndex][c], mix));
                color = {
                    addSat(addSat(scale8(s.colors[0].r, value[0]), scale8(s.colors[1].r, value[1])), scale8(s.colors[2].r, value[2])),
                    addSat(addSat(scale8(s.colors[0].g, value[0]), scale8(s.colors[1].g, value[1])), scale8(s.colors[2].g, value[2])),
                    addSat(addSat(scale8(s.colors[0].b, value[0]), scale8(s.colors[1].b, value[1])), scale8(s.colors[2].b, value[2]))};
                level = 255;
                break;
            }
            case 8: { // Beat advances the active block.
                uint8_t blocks = 2 + (100 - s.width) / 5;
                uint8_t block = (uint32_t)x * blocks >> 16;
                level = block == _step % blocks ? addSat(_volume / 2, _beatPulse) : scale8(spectral, 100);
                color = s.colors[(_step + block) % 3];
                break;
            }
            case 9: { // Audio-fed flame; interpolated noise avoids random full-strip flashes.
                uint16_t tick = _phase >> 10;
                uint8_t fraction = (_phase & 1023) >> 2;
                uint16_t cell = (uint32_t)x * (4 + (100 - s.width)) >> 16;
                uint8_t noise = addSat(scale8(audioNoise(cell + tick * 1973u), 255 - fraction),
                                      scale8(audioNoise(cell + (tick + 1u) * 1973u), fraction));
                uint8_t heat = scale8(addSat(spectral, _bass / 3), 100 + scale8(noise, 155));
                level = scale8(heat, 255 - (x >> 9));
                color = gradient(s, heat);
                break;
            }
            default: break;
        }
        uint8_t* dst = _buf + (size_t)i * 4;
        uint8_t r = scale8(color.r, level), g = scale8(color.g, level), b = scale8(color.b, level);
        if (s.mode == 5) {
            // Stored output already includes brightness. Do not multiply tails a second time.
            r = max(scale8(r, brightness), (uint8_t)((uint16_t)dst[3] * release / 255));
            g = max(scale8(g, brightness), (uint8_t)((uint16_t)dst[2] * release / 255));
            b = max(scale8(b, brightness), (uint8_t)((uint16_t)dst[1] * release / 255));
        } else {
            r = scale8(r, brightness); g = scale8(g, brightness); b = scale8(b, brightness);
        }
        if (!brightness) r = g = b = 0;
        dst[0] = (r || g || b) ? 0xff : 0xe0;
        dst[1] = b; dst[2] = g; dst[3] = r;
    }
    leds.showColumnDirect(_buf, _numLeds);
    const uint8_t onsetRelease = (uint8_t)roundf(255 * powf(160.0f / 255, ticks));
    for (auto& value : _onset) value = (uint16_t)value * onsetRelease / 255;
    if (!input.group) _phase += (uint32_t)motion * (64 + _bass + _mid) / 256;
    uint8_t beatRelease = s.mode == 8 ? (uint8_t)roundf(255 * powf((160 + (100 - s.speed) * 9 / 10) / 255.0f, ticks)) : release;
    _beatPulse = (uint16_t)_beatPulse * beatRelease / 255;
    for (auto& radius : _ripple) {
        if (radius != 65535) {
            uint32_t next = radius + motion;
            radius = next >= 65535 ? 65535 : next;
        }
    }
}

uint32_t AudioReactiveEffect::intervalUs(const EffectParams&) const {
    return 6667; // Target 150 FPS for audio only; the existing driver bandwidth limit still applies.
}
