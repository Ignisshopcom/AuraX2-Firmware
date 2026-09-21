#include "audio_reactive_effect.h"
#include <assert.h>
#include <stdio.h>
#include <set>
#include <vector>

uint32_t testMillis = 0;
class Output : public ILedDriver {
public:
    explicit Output(uint16_t size) : size(size) {}
    uint16_t size;
    std::vector<uint8_t> bytes;
    uint16_t numLeds() const override { return size; }
    void clear() override { bytes.clear(); }
    void showColumnDirect(const uint8_t* data, uint16_t count) override {
        assert(count == size);
        bytes.assign(data, data + count * 4);
        for (size_t i = 0; i < bytes.size(); i += 4) assert(bytes[i] == 0xe0 || bytes[i] == 0xff);
    }
    unsigned energy() const {
        unsigned sum = 0;
        for (size_t i = 0; i < bytes.size(); ++i) if (i % 4) sum += bytes[i];
        return sum;
    }
};

static uint64_t signature(AudioReactiveSettings settings) {
    AudioReactiveInput input;
    AudioReactiveEffect effect(input);
    EffectParams params = {};
    Output output(170);
    input.setSettings(settings);
    effect.reset(params, output.size);
    uint64_t hash = 1469598103934665603ull;
    for (int frame = 0; frame < 240; ++frame) {
        testMillis += 17;
        input.update(40 + frame * 37 % 180, 40 + frame * 7 % 150,
                     20 + frame * 3 % 180, 60 + frame * 9 % 150, frame % 30 == 0 ? 255 : 0);
        effect.update(output, params);
        for (auto byte : output.bytes) { hash ^= byte; hash *= 1099511628211ull; }
    }
    return hash;
}

int main() {
    EffectParams ordinary = {};
    std::set<uint64_t> hashes;
    for (uint8_t mode = 0; mode < 10; ++mode) {
        AudioReactiveInput input;
        AudioReactiveEffect effect(input);
        AudioReactiveSettings settings;
        settings.mode = mode;
        Output output(350);
        effect.reset(ordinary, output.size);
        assert(effect.ready());
        uint64_t hash = 1469598103934665603ull;
        unsigned total = 0;
        for (int frame = 0; frame < 180; ++frame) {
            testMillis += 17;
            settings.speed = frame < 60 ? 0 : frame < 120 ? 50 : 100;
            settings.width = frame % 101;
            settings.decay = frame % 101;
            input.setSettings(settings);
            input.update(180, 120 + frame % 100, 90 + frame % 160, 255, frame % 20 == 0 ? 255 : 0);
            effect.update(output, ordinary);
            total += output.energy();
            for (uint8_t value : output.bytes) { hash ^= value; hash *= 1099511628211ull; }
        }
        assert(total > 0);
        assert(hashes.insert(hash).second); // Ten different renderers, not aliases.
        settings.brightness = 0;
        input.setSettings(settings);
        effect.update(output, ordinary);
        assert(output.energy() == 0);
        settings.brightness = 80;
        input.setSettings(settings);
        for (int frame = 0; frame < 360; ++frame) {
            testMillis += 17;
            input.update(0, 0, 0, 0, 0);
            effect.update(output, ordinary);
        }
        assert(output.energy() == 0);
        auto low = settings, high = settings;
        low.speed = 0; high.speed = 100;
        assert(signature(low) != signature(high));
        low = high = settings;
        low.width = 0; high.width = 100;
        assert(signature(low) != signature(high));
        if (mode == 2 || mode == 3 || mode == 5 || mode == 6) {
            low = high = settings;
            low.decay = 0; high.decay = 100;
            assert(signature(low) != signature(high));
        }
        printf("PASS effect %d: distinct output, extremes, brightness zero, silence\n", mode);
    }
    AudioReactiveInput input;
    AudioReactiveEffect effect(input);
    AudioReactiveSettings settings;
    settings.mode = 1;
    settings.brightness = 100;
    settings.colors[0] = settings.colors[1] = settings.colors[2] = {255, 255, 255};
    input.setSettings(settings);
    Output output(170);
    effect.reset(ordinary, output.size);
    input.update(255, 255, 255, 255, 0);
    effect.update(output, ordinary);
    assert(output.energy() == 170 * 3 * 255); // Full response on the very first frame.
    testMillis += 17;
    effect.update(output, ordinary);
    assert(output.energy() == 170 * 3 * 255); // Hold between packets, no pulsing fade.
    settings.mirror = true;
    input.setSettings(settings);
    input.update(127, 255, 0, 0, 0);
    effect.update(output, ordinary);
    for (size_t i = 0; i < 85; ++i) for (size_t c = 0; c < 4; ++c)
        assert(output.bytes[i * 4 + c] == output.bytes[(169 - i) * 4 + c]);
    for (auto size : {0, 1, 2, 142, 170, 350, 1000}) {
        Output edge(size);
        effect.reset(ordinary, size);
        for (int mode = 0; mode < 10; ++mode) {
            settings.mode = mode;
            input.setSettings(settings);
            effect.update(edge, ordinary);
        }
    }
    printf("PASS immediate attack, packet hold, mirror, LED counts 0..1000\n");
    for (int mode = 0; mode < 10; ++mode) for (int response = 1; response <= 3; ++response) {
        AudioReactiveInput selected;
        AudioReactiveEffect renderer(selected);
        AudioReactiveSettings s;
        s.mode = mode; s.response = response;
        selected.setSettings(s);
        Output strip(170);
        renderer.reset(ordinary, strip.size);
        uint8_t levels[5] = {255, 0, 0, 0, 255}, spectrum[64] = {};
        const int bins[] = {0, 2, 15, 40};
        const int other = response % 3 + 1;
        levels[other] = 255;
        spectrum[bins[other]] = 255;
        for (int frame = 0; frame < 60; ++frame) {
            testMillis += 17;
            selected.updateSpectrum(levels, spectrum);
            renderer.update(strip, ordinary);
            assert(strip.energy() == 0); // Other ranges cannot trigger the selected range.
        }
        levels[other] = 0; levels[response] = 255;
        spectrum[bins[other]] = 0; spectrum[bins[response]] = 255;
        unsigned lit = 0;
        for (int frame = 0; frame < 60; ++frame) {
            testMillis += 17;
            selected.updateSpectrum(levels, spectrum);
            renderer.update(strip, ordinary);
            lit += strip.energy();
        }
        assert(lit > 0);
    }
    std::vector<std::vector<uint8_t>> detail;
    for (int band : {8, 16, 32, 48}) {
        AudioReactiveInput selected;
        AudioReactiveEffect renderer(selected);
        AudioReactiveSettings s;
        s.brightness = 100; s.width = 0; s.speed = 0;
        s.colors[0] = s.colors[1] = s.colors[2] = {255, 255, 255};
        selected.setSettings(s);
        Output strip(170);
        renderer.reset(ordinary, strip.size);
        uint8_t levels[5] = {128, 128, 128, 128, 0}, spectrum[64] = {};
        spectrum[band] = 255;
        selected.updateSpectrum(levels, spectrum);
        renderer.update(strip, ordinary);
        size_t peak = 0;
        for (size_t i = 1; i < 170; ++i) if (strip.bytes[i * 4 + 1] > strip.bytes[peak * 4 + 1]) peak = i;
        assert(abs((int)peak - band * 169 / 63) <= 2);
        for (auto& previous : detail) assert(previous != strip.bytes);
        detail.push_back(strip.bytes);
    }
    puts("PASS 64-band spatial detail and bass/mids/treble isolation in all ten effects");
    for (int mode : {0, 3, 4, 7, 8, 9}) {
        AudioReactiveInput musical;
        AudioReactiveEffect renderer(musical);
        AudioReactiveSettings s;
        s.mode = mode; s.response = 0; s.brightness = 100;
        musical.setSettings(s);
        Output strip(170);
        renderer.reset(ordinary, strip.size);
        assert(renderer.intervalUs(ordinary) == 6667);
        unsigned visible = 0;
        std::set<unsigned> energies;
        for (int frame = 0; frame < 300; ++frame) {
            testMillis += 10;
            uint8_t level = 20 + (frame % 100 < 50 ? frame % 50 : 50 - frame % 50) * 2;
            uint8_t levels[5] = {level, level, uint8_t(level / 2), uint8_t(level / 4), 0};
            uint8_t spectrum[64];
            for (int i = 0; i < 64; ++i) spectrum[i] = level * (64 - i) / 64;
            musical.updateSpectrum(levels, spectrum);
            renderer.update(strip, ordinary);
            if (strip.energy()) ++visible;
            energies.insert(strip.energy());
        }
        assert(visible == 300); // Continuous music must not require discrete beat packets.
        assert(energies.size() > 30);
    }
    puts("PASS continuous non-beat dynamics in Spectrum/Ripple/Waves/Color Flow/Steps/Fire");
    {
        AudioReactiveInput musical;
        AudioReactiveEffect renderer(musical);
        AudioReactiveSettings s; s.mode = 7; s.speed = 50; s.brightness = 100;
        s.colors[0] = {255,0,0}; s.colors[1] = {0,255,0}; s.colors[2] = {0,0,255};
        musical.setSettings(s);
        Output strip(170); renderer.reset(ordinary, strip.size);
        for (int frame = 0; frame < 90; ++frame) {
            testMillis += 10;
            musical.update(200, frame < 30 ? 200 : 0, frame >= 30 && frame < 60 ? 200 : 0, frame >= 60 ? 200 : 0, 0);
            renderer.update(strip, ordinary);
        }
        int red = -1, green = -1, blue = -1;
        for (int i = 0; i < 170; ++i) {
            if (strip.bytes[i*4+3] > 100) red = i;
            if (strip.bytes[i*4+2] > 100) green = i;
            if (strip.bytes[i*4+1] > 100) blue = i;
        }
        assert(blue >= 0 && green > blue && red > green);
        for (int frame = 0; frame < 500; ++frame) {
            testMillis += 10; musical.update(0,0,0,0,0); renderer.update(strip, ordinary);
        }
        assert(strip.energy() == 0);
    }
    puts("PASS ordered bass/mid/treble music history and eventual silence");
    {
        // 50 Hz source, 150 Hz renderer: in-between frames must change, not hold/jump.
        AudioReactiveInput musical;
        AudioReactiveEffect renderer(musical);
        AudioReactiveSettings s; s.mode = 2; s.speed = 0; s.brightness = 100;
        s.colors[0] = s.colors[1] = s.colors[2] = {255,255,255};
        musical.setSettings(s);
        Output strip(170); renderer.reset(ordinary, strip.size);
        musical.update(0,0,0,0,0); renderer.update(strip, ordinary);
        musical.update(220,220,0,0,0);
        unsigned previous = 0;
        for (int frame = 0; frame < 3; ++frame) {
            testMillis += 7;
            renderer.update(strip, ordinary);
            assert(strip.energy() > previous);
            previous = strip.energy();
        }
        for (int frame = 0; frame < 10; ++frame) {
            testMillis += 7; musical.update(220,220,0,0,0); renderer.update(strip, ordinary);
        }
        assert(previous > strip.energy() * 0.8); // At least 80% output in 21 ms.
        previous = strip.energy();
        musical.update(0,0,0,0,0);
        testMillis += 7; renderer.update(strip, ordinary);
        assert(strip.energy() > 0 && strip.energy() < previous);
        for (int frame = 0; frame < 30; ++frame) { testMillis += 7; renderer.update(strip, ordinary); }
        assert(strip.energy() == 0); // No ghost audio after a stalled source.
    }
    {
        // Visual smoothing uses elapsed time, not an FPS-dependent per-frame multiplier.
        std::set<unsigned> outputs;
        for (int dt : {3,6,7,14}) {
            AudioReactiveInput musical; AudioReactiveEffect renderer(musical);
            AudioReactiveSettings s; s.mode = 2; s.speed = 0; s.brightness = 100;
            s.colors[0] = s.colors[1] = s.colors[2] = {255,255,255};
            musical.setSettings(s);
            Output strip(170); renderer.reset(ordinary, strip.size);
            musical.update(0,0,0,0,0); renderer.update(strip, ordinary);
            musical.update(180,180,0,0,0);
            for (int t = 0; t < 42; t += dt) { testMillis += dt; renderer.update(strip, ordinary); }
            outputs.insert(strip.energy());
        }
        assert(outputs.size() == 1);
    }
    puts("PASS intermediate 150 FPS frames, bounded attack, silence and time-based smoothing");
    for (int mode = 0; mode < 10; ++mode) {
        AudioReactiveInput inputs[10];
        AudioReactiveEffect* effects[10];
        AudioReactiveSettings s; s.mode = mode;
        for (int i = 0; i < 10; ++i) {
            testMillis = i * 131;
            inputs[i].setSettings(s);
            effects[i] = new AudioReactiveEffect(inputs[i]);
            effects[i]->reset(ordinary, 170);
        }
        AudioGroup::Packet p; p.mask = 1; p.session[0] = 77;
        for (int frame = 0; frame < 150; ++frame) {
            p.ageMs = frame * 17;
            if (frame % 20 == 0) { p.beatAt[p.beats % 4] = p.ageMs; ++p.beats; }
            p.levels[0] = 160; p.levels[1] = 60 + frame % 120; p.levels[2] = 100; p.levels[3] = 80;
            for (int b = 0; b < 64; ++b) p.spectrum[b] = (frame + b * 13) % 200;
            std::vector<uint8_t> reference;
            for (int i = 0; i < 10; ++i) {
                // Independent local clocks; same shared frame and presentation age.
                testMillis = i * 131 + frame * 17;
                inputs[i].updateGroup(p, p.ageMs);
                testMillis += 7;
                Output strip(170);
                effects[i]->update(strip, ordinary);
                if (i == 0) reference = strip.bytes;
                else assert(strip.bytes == reference);
            }
        }
        for (auto* effect : effects) delete effect;
    }
    puts("PASS identical ten-receiver render output with independent clocks for all ten modes");
}
