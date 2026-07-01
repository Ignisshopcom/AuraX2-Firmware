#pragma once

#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// LED strip type — change to LED_TYPE_WS281X for WS2812B/WS2811 strips
#define LED_TYPE_APA102  1
#define LED_TYPE_WS281X  0
#define LED_TYPE         LED_TYPE_WS281X

// APA102 pins (SPI, used when LED_TYPE == LED_TYPE_APA102)
#define LED_DATA_PIN  6
#define LED_CLK_PIN   5
#define APA102_SPI_MHZ 15

// WS281x data pin (RMT single-wire, used when LED_TYPE == LED_TYPE_WS281X)
#define WS_DATA_PIN   6

#define NUM_LEDS      45

#define PIX_FILE      "/show.pix"

// Safety guard used when no valid runtime current limit is stored.
// Keeps full SPI speed, but clamps sudden all-white current spikes.
#define DEFAULT_CURRENT_LIMIT_MA 1000
#define APA102_AUTO_CURRENT_LIMIT_MA DEFAULT_CURRENT_LIMIT_MA

// Odkomentuj pro debug výpisy na sériový port
// #define PIX_DEBUG

#ifdef PIX_DEBUG
  #define LOG(...)  do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)
  #define LOGLN(s)  do { if (Serial) Serial.println(s); } while (0)
#else
  #define LOG(...)  ((void)0)
  #define LOGLN(s)  ((void)0)
#endif
