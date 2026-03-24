#pragma once

#include <driver/spi_master.h>
#include <stdint.h>
#include "led_driver.h"

class APA102 : public ILedDriver {
public:
    // dataPin = MOSI, clkPin = SCK
    // XIAO ESP32S3 default SPI: dataPin=9 (D10), clkPin=7 (D8)
    APA102(uint8_t dataPin, uint8_t clkPin, uint16_t numLeds,
           spi_host_device_t host = SPI2_HOST);
    ~APA102();

    // freqHz: APA102 supports up to ~20 MHz reliably
    bool begin(uint32_t freqHz = 20000000);

    void setPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 31);
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 31);
    void clear() override;

    // Blocking — waits until transfer is done
    void show();

    // Non-blocking — queues DMA transfer, returns immediately.
    // Waits for the *previous* transfer at the start, so CPU and DMA overlap:
    //   setPixels(); showAsync();   ← queues frame N, DMA starts
    //   setPixels(); showAsync();   ← waits frame N done, queues frame N+1
    void showAsync();
    void waitForShow();

    // Direct path for pix player — pixData is [0xE0|dim, B, G, R] per LED,
    // exactly the format stored in .pix files. No conversion needed.
    // count: number of LEDs in pixData (clamped to numLeds).
    void showColumnDirect(const uint8_t* pixData, uint16_t count) override;

    uint16_t numLeds() const override { return _numLeds; }

private:
    void buildTxBuffer(uint8_t* dst);

    uint8_t  _dataPin, _clkPin;
    uint16_t _numLeds;
    spi_host_device_t _host;
    spi_device_handle_t _spi = nullptr;

    uint8_t* _pixels  = nullptr;   // [r, g, b, brightness] per pixel, internal DRAM
    uint8_t* _txBuf[2] = {};       // double DMA buffers
    spi_transaction_t _trans[2]  = {};
    int  _curBuf     = 0;
    bool _txInFlight = false;
    size_t _txLen    = 0;
};
