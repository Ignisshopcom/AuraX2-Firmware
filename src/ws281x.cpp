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

void WS281x::setBrightness(uint8_t pct) {
    _globalBrightness = pct > 100 ? 100 : pct;
}

void WS281x::setReverse(bool reverse) {
    _reverse = reverse;
}

void WS281x::setMirror(bool mirror) {
    _mirror = mirror;
}

void WS281x::setCurrentLimit(uint16_t mALimit, uint16_t mAPerLed) {
    _mALimit  = mALimit;
    _mAPerLed = mAPerLed > 0 ? mAPerLed : 1;
}

uint16_t WS281x::maxRefreshHz() const {
    // WS281x sends 24 bits/LED at 800 kHz (~30 us/LED) plus reset/latch time.
    // Keep a safety margin for RMT driver overhead and the player task.
    uint32_t frameUs = (uint32_t)_numLeds * 30u + 80u;
    if (frameUs == 0) return 1;
    uint32_t hz = (1000000u / frameUs) * 9u / 10u;
    if (hz < 1) hz = 1;
    if (hz > 2500) hz = 2500;
    return (uint16_t)hz;
}

static uint16_t mappedLedIndex(uint16_t outIndex, uint16_t count, bool reverse, bool mirror) {
    if (count < 2) return 0;
    uint16_t mapped = outIndex;
    if (!mirror) return reverse ? (uint16_t)(count - 1 - outIndex) : outIndex;
    if (count & 1) {
        uint16_t center = count / 2;
        mapped = (outIndex <= center) ? (uint16_t)(center - outIndex) : (uint16_t)(outIndex - center);
    } else {
        uint16_t right = count / 2;
        mapped = (outIndex < right) ? (uint16_t)(right - 1 - outIndex) : (uint16_t)(outIndex - right);
    }
    return reverse ? (uint16_t)(count - 1 - mapped) : mapped;
}

// Encode .pix column data [0xE0|bri, B, G, R] × count into RMT items.
// Output: GRB bit stream, MSB first (WS2812B wire order).
// scale256: Q8 current-limit scale factor (256 = no limiting).
void WS281x::encodePixels(rmt_item32_t* dst, const uint8_t* pixData, uint16_t count, uint16_t scale256) {
    for (uint16_t i = 0; i < count; i++) {
        const uint16_t srcIndex = mappedLedIndex(i, count, _reverse, _mirror);
        const uint8_t* p = pixData + (size_t)srcIndex * 4;
        uint8_t bri = p[0] & 0x1F;
        uint8_t b   = p[1];
        uint8_t g   = p[2];
        uint8_t r   = p[3];

        if (_globalBrightness > 0) {
            // Global override: scale RGB by percentage, ignore per-pixel bri
            r = (uint8_t)((r * _globalBrightness) / 100);
            g = (uint8_t)((g * _globalBrightness) / 100);
            b = (uint8_t)((b * _globalBrightness) / 100);
        } else if (bri > 0 && bri < 31) {
            // Per-pixel APA102-style brightness from .pix file
            // bri=0 znamená "brightness není použit" (WS281x soubory) → plný jas.
            r = (uint8_t)((r * bri) / 31);
            g = (uint8_t)((g * bri) / 31);
            b = (uint8_t)((b * bri) / 31);
        }

        if (scale256 < 256) {
            r = (uint8_t)(r * scale256 >> 8);
            g = (uint8_t)(g * scale256 >> 8);
            b = (uint8_t)(b * scale256 >> 8);
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

    // Current limiting: pre-pass to estimate draw, compute scale factor
    uint16_t scale256 = 256;
    if (_mALimit > 0) {
        uint32_t totalRGB = 0;
        for (uint16_t i = 0; i < n; i++) {
            const uint16_t srcIndex = mappedLedIndex(i, n, _reverse, _mirror);
            const uint8_t* p = pixData + (size_t)srcIndex * 4;
            uint8_t bri = p[0] & 0x1F;
            uint32_t r = p[3], g = p[2], b = p[1];
            if (_globalBrightness > 0) {
                r = r * _globalBrightness / 100u;
                g = g * _globalBrightness / 100u;
                b = b * _globalBrightness / 100u;
            } else if (bri > 0 && bri < 31) {
                r = r * bri / 31u;
                g = g * bri / 31u;
                b = b * bri / 31u;
            }
            totalRGB += r + g + b;
        }
        uint32_t estMA = n + totalRGB * _mAPerLed / 765u;
        if (estMA > _mALimit) {
            scale256 = (uint16_t)((uint32_t)_mALimit * 256u / estMA);
        }
    }

    encodePixels(_rmtBuf[buf], pixData, n, scale256);

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
