#include <Arduino.h>
#include "config.h"
#include "pix_player.h"
#include "wifi_control.h"
#include "sync_control.h"

#if LED_TYPE == LED_TYPE_WS281X
  #include "ws281x.h"
  WS281x    leds(WS_DATA_PIN, NUM_LEDS);
#else
  #include "apa102.h"
  APA102    leds(LED_DATA_PIN, LED_CLK_PIN, NUM_LEDS);
#endif

PixPlayer   player(leds);
SyncControl syncCtrl(player);
WifiControl wifi(player, WIFI_SSID, WIFI_PASSWORD, &syncCtrl);

void setup() {
    Serial.begin(115200);
#ifdef PIX_DEBUG
    delay(1500);
#endif

#if LED_TYPE == LED_TYPE_WS281X
    if (!leds.begin()) {
        Serial.println("[ws281x] init failed");
        while (true);
    }
#else
    if (!leds.begin(20000000)) {
        Serial.println("[apa102] init failed");
        while (true);
    }
    leds.clear();
    leds.show();
#endif

if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed");
        return;
    }
    if (LittleFS.exists(PIX_FILE)) {
        int err = player.load(PIX_FILE);
        if (err) { Serial.printf("[pix] load failed: %d\n", err); return; }
        player.startTask(1);  // core 1, priorita 5
    } else {
        Serial.printf("[pix] soubor nenalezen: %s\n", PIX_FILE);
    }

    xTaskCreatePinnedToCore(
        [](void* arg) {
            WiFi.mode(WIFI_STA);
            syncCtrl.begin();
            auto* w = static_cast<WifiControl*>(arg);
            if (w->begin())
                Serial.println("[wifi] server ready");
            else
                Serial.println("[wifi] offline — server not started");
            while (true) { w->handle(); vTaskDelay(1); }
        },
        "wifi_ctrl", 4096, &wifi, 2, nullptr, 0  // core 0, priorita 2
    );
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
