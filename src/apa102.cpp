#include "apa102.h"
#include "config.h"
#include <esp_heap_caps.h>
#include <string.h>

// APA102 frame layout:
//   Start : 4 bytes 0x00
//   LEDs  : 4 bytes each — [0xE0|brightness, B, G, R]
//   End   : ceil(N/2) clock pulses = ceil(N/16) bytes 0xFF

APA102::APA102(uint8_t dataPin, uint8_t clkPin, uint16_t numLeds, spi_host_device_t host)
    : _dataPin(dataPin), _clkPin(clkPin), _numLeds(numLeds), _host(host) {}

APA102::~APA102() {
    if (_spi) {
        waitForShow();
        spi_bus_remove_device(_spi);
        spi_bus_free(_host);
    }
    free(_pixels);
    heap_caps_free(_txBuf[0]);
    heap_caps_free(_txBuf[1]);
}

bool APA102::begin(uint32_t freqHz) {
    _pixels = (uint8_t*)malloc(_numLeds * 4);
    if (!_pixels) return false;

    // Default: black, full brightness
    for (int i = 0; i < _numLeds; i++) {
        _pixels[i * 4 + 0] = 0;   // r
        _pixels[i * 4 + 1] = 0;   // g
        _pixels[i * 4 + 2] = 0;   // b
        _pixels[i * 4 + 3] = 31;  // brightness
    }

    size_t endBytes = (_numLeds + 15) / 16;
    _txLen = 4 + _numLeds * 4 + endBytes;
    _txLen = (_txLen + 3) & ~3u;   // align to 4 bytes for DMA

    for (int i = 0; i < 2; i++) {
        _txBuf[i] = (uint8_t*)heap_caps_malloc(_txLen, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!_txBuf[i]) return false;

        memset(_txBuf[i], 0, _txLen);
        // Pre-fill end frame
        memset(_txBuf[i] + 4 + _numLeds * 4, 0xFF, endBytes);
    }

    spi_bus_config_t bus = {
        .mosi_io_num   = _dataPin,
        .miso_io_num   = -1,
        .sclk_io_num   = _clkPin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)_txLen,
    };
    if (spi_bus_initialize(_host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t dev = {
        .mode            = 0,
        .clock_speed_hz  = (int)freqHz,
        .spics_io_num    = -1,
        .queue_size      = 2,
    };
    return spi_bus_add_device(_host, &dev, &_spi) == ESP_OK;
}

// ── pixel helpers ──────────────────────────────────────────────────────────

void APA102::setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (index >= _numLeds) return;
    uint8_t* p  = _pixels + index * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = brightness & 0x1F;
}

void APA102::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    brightness &= 0x1F;
    for (int i = 0; i < _numLeds; i++) {
        uint8_t* p = _pixels + i * 4;
        p[0] = r; p[1] = g; p[2] = b; p[3] = brightness;
    }
}

void APA102::clear() {
    for (int i = 0; i < _numLeds; i++) {
        uint8_t* p = _pixels + i * 4;
        p[0] = p[1] = p[2] = 0;
        p[3] = 31;
    }
    show();
}

// ── transfer ───────────────────────────────────────────────────────────────

void APA102::buildTxBuffer(uint8_t* buf) {
    // Start frame is already zeroed (set in begin())
    uint8_t* dst = buf + 4;
    const uint8_t* src = _pixels;
    for (int i = 0; i < _numLeds; i++, src += 4, dst += 4) {
        dst[0] = 0xE0 | src[3];  // brightness
        dst[1] = src[2];         // B
        dst[2] = src[1];         // G
        dst[3] = src[0];         // R
    }
    // End frame is already 0xFF (set in begin())
}

void APA102::waitForShow() {
    if (!_txInFlight) return;
    spi_transaction_t* result;
    spi_device_get_trans_result(_spi, &result, portMAX_DELAY);
    _txInFlight = false;
}

void APA102::showAsync() {
    // Wait for the previous transfer so its buffer is free to reuse
    waitForShow();

    int buf = _curBuf;
    buildTxBuffer(_txBuf[buf]);

    spi_transaction_t* t = &_trans[buf];
    memset(t, 0, sizeof(*t));
    t->length    = _txLen * 8;      // SPI length is in bits
    t->tx_buffer = _txBuf[buf];

    spi_device_queue_trans(_spi, t, portMAX_DELAY);
    _txInFlight = true;
    _curBuf = 1 - buf;
}

void APA102::show() {
    showAsync();
    waitForShow();
}

void APA102::setBrightness(uint8_t pct) {
    _globalBrightness = pct > 100 ? 100 : pct;
}

void APA102::setReverse(bool reverse) {
    _reverse = reverse;
}

void APA102::setMirror(bool mirror) {
    _mirror = mirror;
}

void APA102::setContactPoi(bool enabled) {
    _contactPoi = enabled;
}

void APA102::setCurrentLimit(uint16_t mALimit, uint16_t mAPerLed) {
    _mALimit  = mALimit > 0 ? mALimit : APA102_AUTO_CURRENT_LIMIT_MA;
    _mAPerLed = mAPerLed > 0 ? mAPerLed : 1;
    _currentScale256 = 256;
}

static uint16_t contactPoiLogicalCount(uint16_t physicalCount, bool contactPoi) {
    if (!contactPoi) return physicalCount;
    return physicalCount <= 20 ? 1 : (uint16_t)(physicalCount - 19);
}

uint16_t APA102::logicalNumLeds() const {
    return contactPoiLogicalCount(_numLeds, _contactPoi);
}

static uint16_t contactPoiLogicalIndex(uint16_t physicalIndex, bool contactPoi) {
    if (!contactPoi) return physicalIndex;
    return physicalIndex < 20 ? 0 : (uint16_t)(physicalIndex - 19);
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

void APA102::showColumnDirect(const uint8_t* pixData, uint16_t count) {
    waitForShow();

    int buf = _curBuf;
    uint8_t* dst = _txBuf[buf] + 4;  // skip start frame
    uint16_t logicalCount = logicalNumLeds();
    uint16_t n = (count < logicalCount) ? count : logicalCount;

    // Current limiting: pre-pass to estimate draw. Power spikes are clamped
    // immediately, then brightness ramps back up slowly to avoid BMS trips.
    uint16_t targetScale256 = 256;  // Q8: 256 = no scaling
    if (_mALimit > 0) {
        uint32_t totalRGB = 0;
        uint16_t activeLeds = 0;
        for (uint16_t i = 0; i < _numLeds; i++) {
            const uint16_t logicalIndex = contactPoiLogicalIndex(i, _contactPoi);
            if (logicalIndex >= n) continue;
            const uint16_t srcIndex = mappedLedIndex(logicalIndex, n, _reverse, _mirror);
            const uint8_t* p = pixData + (size_t)srcIndex * 4;
            uint8_t nibble;
            if (_globalBrightness > 0) {
                nibble = (uint8_t)((_globalBrightness * 31u + 50u) / 100u);
                if (nibble == 0) nibble = 1;
            } else {
                nibble = (p[0] == 0xE0) ? 6u : (p[0] & 0x1Fu);
            }
            totalRGB += ((uint32_t)p[3] + p[2] + p[1]) * nibble / 31u;
            activeLeds++;
        }
        uint32_t estMA = activeLeds + totalRGB * _mAPerLed / 765u;
        if (estMA > _mALimit) {
            targetScale256 = (uint16_t)((uint32_t)_mALimit * 256u / estMA);
        }
    }
    uint16_t scale256 = targetScale256;
    if (_mALimit > 0) {
        constexpr uint16_t kScaleStepUp = 4;
        if (targetScale256 < _currentScale256) {
            _currentScale256 = targetScale256;
        } else if (targetScale256 > _currentScale256) {
            uint16_t delta = targetScale256 - _currentScale256;
            _currentScale256 += delta > kScaleStepUp ? kScaleStepUp : delta;
        }
        scale256 = _currentScale256;
    } else {
        _currentScale256 = 256;
    }

    for (uint16_t i = 0; i < _numLeds; i++, dst += 4) {
        const uint16_t logicalIndex = contactPoiLogicalIndex(i, _contactPoi);
        if (logicalIndex >= n) {
            dst[0] = 0xE0;
            dst[1] = dst[2] = dst[3] = 0;
            continue;
        }
        const uint16_t srcIndex = mappedLedIndex(logicalIndex, n, _reverse, _mirror);
        const uint8_t* p = pixData + (size_t)srcIndex * 4;
        if (_globalBrightness > 0) {
            // Global override: map pct 1–100 to APA102 nibble 1–31
            uint8_t nibble = (uint8_t)((_globalBrightness * 31u + 50u) / 100u);
            if (nibble == 0) nibble = 1;
            dst[0] = 0xE0 | nibble;
        } else {
            // Passthrough — bri=0 (0xE0) means no scaling in .pix files → map to 20% (6/31)
            dst[0] = (p[0] == 0xE0) ? 0xE6 : p[0];
        }
        dst[1] = (uint8_t)(p[1] * scale256 >> 8);
        dst[2] = (uint8_t)(p[2] * scale256 >> 8);
        dst[3] = (uint8_t)(p[3] * scale256 >> 8);
    }

    spi_transaction_t* t = &_trans[buf];
    memset(t, 0, sizeof(*t));
    t->length    = _txLen * 8;
    t->tx_buffer = _txBuf[buf];

    spi_device_queue_trans(_spi, t, portMAX_DELAY);
    _txInFlight = true;
    _curBuf = 1 - buf;
}
