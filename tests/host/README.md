# Audio Renderer Host Tests

These tests compile the actual firmware audio renderer with a minimal Arduino clock/lock stub.
They do not emulate FreeRTOS, Wi-Fi, DMA, power limits, or physical LED timing.

From the project root, with a host C++11 compiler:

```sh
c++ -std=c++11 -O0 -g -I tests/host -I src tests/host/audio_effect_test.cpp src/audio_reactive_effect.cpp -o audio_effect_test
./audio_effect_test
```

Coverage: ten distinct outputs, every exposed effect parameter changes rendering, immediate
attack, packet hold, symmetry, zero brightness, silence and LED counts from zero to 1000.
Production builds never include the Arduino stub.

## Wi-Fi reconnect

Run `node tests/host/run_wifi_reconnect_test.cjs` with `CXX` pointing to a host
C++ compiler (including `zig.exe`). The runner extracts the production Wi-Fi
event handlers and reconnect functions into a temporary translation unit.
Optional `AURAX_IPHONE_REFERENCE` points to the tested candidate's
`src/wifi_control.cpp` and checks exact equality of the imported logic.

Checks cover first-framework-retry grace, exponential retry delays, watchdog,
DHCP waiting, established connections, disabled retries, intentional disconnect,
30-second stable AP fallback, missing SSID, and millisecond counter wrap.
Radio behavior, real event concurrency, and phone compatibility require hardware
testing; these host tests do not emulate the ESP32 Wi-Fi driver.
