#include "ws281x.h"
#include <esp_heap_caps.h>
#include <string.h>

// RMT clock: 80 MHz / 2 = 40 MHz → 25 ns per tick
//
// WS2812B timing:
//   Bit 1 : T1H = 800 ns (32 ticks) HIGH,  T1L = 450 ns (18 ticks) LOW
//   Bit 0 : T0H = 400 ns (16 ticks) HIGH,  T0L = 850 ns (34 ticks) LOW
//   Reset : 50 µs (2000 ticks) LOW
static const rmt_item32_t WS_BIT_1 = {{{ 32, 1, 18, 0 }}};
static const rmt_item32_t WS_BIT_0 = {{{ 16, 1, 34, 0 }}};
static const rmt_item32_t WS_RESET = {{{ 2000, 0, 0, 0 }}};  // 50 µs LOW reset

WS281x::WS281x(uint8_t dataPin, uint16_t numLeds, rmt_channel_t channel)
    : _dataPin(dataPin), _numLeds(numLeds), _channel(channel) {}

WS281x::~WS281x() {
    waitForShow();
    rmt_driver_uninstall(_channel);
    heap_caps_free(_rmtBuf[0]);
    heap_caps_free(_rmtBuf[1]);
}

bool WS281x::begin() {
    // 24 bits per LED + 1 reset item
    _itemCount = (size_t)_numLeds * 24 + 1;
    size_t bufBytes = _itemCount * sizeof(rmt_item32_t);

    for (int i = 0; i < 2; i++) {
        _rmtBuf[i] = (rmt_item32_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!_rmtBuf[i]) return false;
        // Pre-fill reset item at end — never changes
        _rmtBuf[i][_itemCount - 1] = WS_RESET;
    }

    rmt_config_t config = {};
    config.rmt_mode             = RMT_MODE_TX;
    config.channel              = _channel;
    config.gpio_num             = (gpio_num_t)_dataPin;
    config.clk_div              = 2;    // 80 MHz / 2 = 40 MHz (25 ns/tick)
    config.mem_block_num        = 1;
    config.tx_config.carrier_en       = false;
    config.tx_config.idle_output_en   = true;
    config.tx_config.idle_level       = RMT_IDLE_LEVEL_LOW;
    config.tx_config.loop_en          = false;

    if (rmt_config(&config) != ESP_OK) return false;
    if (rmt_driver_install(_channel, 0, 0) != ESP_OK) return false;
    return true;
}

void WS281x::clear() {
    // Send all-zero pixel data (black)
    static const uint8_t black[4] = {0xE0, 0, 0, 0};
    uint8_t* buf = (uint8_t*)alloca(_numLeds * 4);
    for (int i = 0; i < _numLeds; i++) memcpy(buf + i * 4, black, 4);
    showColumnDirect(buf, _numLeds);
    waitForShow();
}

void WS281x::waitForShow() {
    if (!_txInFlight) return;
    rmt_wait_tx_done(_channel, portMAX_DELAY);
    _txInFlight = false;
}

// Encode .pix column data [0xE0|bri, B, G, R] × count into RMT items.
// Output: GRB bit stream, MSB first (WS2812B wire order).
void WS281x::encodePixels(rmt_item32_t* dst, const uint8_t* pixData, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* p = pixData + i * 4;
        uint8_t bri = p[0] & 0x1F;
        uint8_t b   = p[1];
        uint8_t g   = p[2];
        uint8_t r   = p[3];

        // Apply APA102-style brightness to RGB.
        // bri=0 znamená "brightness není použit" (WS281x soubory) → plný jas.
        if (bri > 0 && bri < 31) {
            r = (uint8_t)((r * bri) / 31);
            g = (uint8_t)((g * bri) / 31);
            b = (uint8_t)((b * bri) / 31);
        }

        // Emit 24 bits: G7..G0, R7..R0, B7..B0 (WS2812B GRB order)
        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
        for (int bit = 23; bit >= 0; bit--) {
            *dst++ = (grb >> bit) & 1 ? WS_BIT_1 : WS_BIT_0;
        }
    }
}

void WS281x::showColumnDirect(const uint8_t* pixData, uint16_t count) {
    // Wait for previous frame before touching the buffer it used
    waitForShow();

    int buf = _curBuf;
    uint16_t n = count < _numLeds ? count : _numLeds;

    encodePixels(_rmtBuf[buf], pixData, n);

    // Fill remaining LEDs with off (all-zero bits)
    if (n < _numLeds) {
        rmt_item32_t* dst = _rmtBuf[buf] + (size_t)n * 24;
        uint16_t remaining = _numLeds - n;
        for (uint16_t j = 0; j < remaining * 24; j++) {
            dst[j] = WS_BIT_0;
        }
    }
    // _rmtBuf[buf][_itemCount-1] is the pre-filled WS_RESET

    rmt_write_items(_channel, _rmtBuf[buf], (int)_itemCount, false);
    _txInFlight = true;
    _curBuf = 1 - buf;
}
