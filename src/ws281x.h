#pragma once

#include "led_driver.h"
#include <driver/rmt.h>

// WS2812B / WS2811 single-wire LED strip driver via ESP32 RMT peripheral.
// Pixel input: [0xE0|brightness, B, G, R] — .pix wire format.
// Brightness is applied by scaling RGB before output (WS281x has no brightness byte).
// Output order: GRB (WS2812B standard).
class WS281x : public ILedDriver {
public:
    // dataPin: GPIO for the single data line
    // channel: RMT TX channel (RMT_CHANNEL_0..3), default 0
    WS281x(uint8_t dataPin, uint16_t numLeds, rmt_channel_t channel = RMT_CHANNEL_0);
    ~WS281x();

    // Initialize RMT peripheral. Call once before use.
    bool begin();

    // Wait until the in-flight RMT transfer completes.
    void waitForShow();

    // ILedDriver —————————————————————————————————————————
    // pixData: [0xE0|bri, B, G, R] per LED (same as .pix format).
    // Async: encodes into double buffer, queues RMT transfer, returns immediately.
    // Waits for the *previous* transfer at the start (CPU/RMT overlap).
    void showColumnDirect(const uint8_t* pixData, uint16_t count) override;
    uint16_t numLeds() const override { return _numLeds; }

private:
    void encodePixels(rmt_item32_t* dst, const uint8_t* pixData, uint16_t count);

    uint8_t       _dataPin;
    uint16_t      _numLeds;
    rmt_channel_t _channel;

    rmt_item32_t* _rmtBuf[2] = {};   // double buffers in DRAM
    int           _curBuf     = 0;
    bool          _txInFlight = false;
    size_t        _itemCount  = 0;   // numLeds*24 + 1 (reset item)
};
