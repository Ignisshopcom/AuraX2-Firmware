#include <Arduino.h>
#include "config.h"
#include "pix_player.h"

#if LED_TYPE == LED_TYPE_WS281X
  #include "ws281x.h"
  WS281x    leds(WS_DATA_PIN, NUM_LEDS);
#else
  #include "apa102.h"
  APA102    leds(LED_DATA_PIN, LED_CLK_PIN, NUM_LEDS);
#endif

PixPlayer player(leds);

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
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // loop() nemá co dělat
}
