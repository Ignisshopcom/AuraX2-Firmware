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
