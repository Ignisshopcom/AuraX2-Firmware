#include <Arduino.h>
#include "config.h"
#include "apa102.h"
#include "pix_player.h"
#include "wifi_control.h"

APA102      leds(LED_DATA_PIN, LED_CLK_PIN, NUM_LEDS);
PixPlayer   player(leds);
WifiControl wifi(player, WIFI_SSID, WIFI_PASSWORD);

void setup() {
    Serial.begin(115200);

    if (!leds.begin(20000000)) {
        Serial.println("[apa102] init failed");
        while (true);
    }
    leds.clear();
    leds.show();

    wifi.begin();   // timeout 10s — pokračuje i bez WiFi

    // Autoplay pokud soubor existuje
    if (LittleFS.exists(PIX_FILE)) {
        int err = player.load(PIX_FILE);
        if (err) Serial.printf("[pix] autoplay failed: %d\n", err);
    }
}

void loop() {
    wifi.handle();    // zpracuje HTTP požadavky
    player.update();  // zobrazí další sloupec pokud je čas
}
