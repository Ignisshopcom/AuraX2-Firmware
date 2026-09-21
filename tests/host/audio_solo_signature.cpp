#include <audio_reactive_effect.h>
#include <stdio.h>
#include <vector>

uint32_t testMillis = 0;
class Output : public ILedDriver {
public:
    std::vector<uint8_t> bytes;
    uint16_t numLeds() const override { return 170; }
    void clear() override { bytes.clear(); }
    void showColumnDirect(const uint8_t* data, uint16_t count) override {
        bytes.assign(data, data + count * 4);
    }
};

// Compile unchanged against both the saved 0.3.3 renderer and the current renderer.
int main() {
    for (int detailed = 0; detailed < 2; ++detailed) {
        for (int mode = 0; mode < 10; ++mode) {
            for (int response = 0; response < 4; ++response) {
                testMillis = 500;
                AudioReactiveInput input;
                AudioReactiveEffect effect(input);
                EffectParams params{};
                AudioReactiveSettings settings;
                settings.mode = mode; settings.response = response;
                Output out;
                effect.reset(params, 170);
                uint64_t hash = 1469598103934665603ull;
                for (int n = 0; n < 600; ++n) {
                    testMillis += 7;
                    settings.speed = (n / 40 * 17) % 101;
                    settings.width = n / 6 % 101;
                    settings.decay = n / 8 % 101;
                    settings.mirror = n >= 400;
                    input.setSettings(settings);
                    if (n % 3 == 0) {
                        uint8_t levels[5] = {uint8_t(n % 256), uint8_t(n*13 % 256),
                            uint8_t(n*7 % 256), uint8_t(n*3 % 256), uint8_t(n % 30 == 0 ? 255 : 0)};
                        uint8_t spectrum[64];
                        for (int k = 0; k < 64; ++k) spectrum[k] = (n*17 + k*13) % 256;
                        if (detailed) input.updateSpectrum(levels, spectrum);
                        else input.update(levels[0], levels[1], levels[2], levels[3], levels[4]);
                    }
                    effect.update(out, params);
                    for (auto byte : out.bytes) { hash ^= byte; hash *= 1099511628211ull; }
                }
                printf("%d/%d/%d %016llx\n", detailed, mode, response, (unsigned long long)hash);
            }
        }
    }
}
