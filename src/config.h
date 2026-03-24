#pragma once

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// LED strip type — change to LED_TYPE_WS281X for WS2812B/WS2811 strips
#define LED_TYPE_APA102  0
#define LED_TYPE_WS281X  1
#define LED_TYPE         LED_TYPE_WS281X

// APA102 pins (SPI, used when LED_TYPE == LED_TYPE_APA102)
#define LED_DATA_PIN  6
#define LED_CLK_PIN   5

// WS281x data pin (RMT single-wire, used when LED_TYPE == LED_TYPE_WS281X)
#define WS_DATA_PIN   6

#define NUM_LEDS      45

#define PIX_FILE      "/show.pix"

// Odkomentuj pro debug výpisy pix playeru
#define PIX_DEBUG
