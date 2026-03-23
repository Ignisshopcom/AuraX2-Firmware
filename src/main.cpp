#include <Arduino.h>
#include "config.h"
#include "apa102.h"
#include "pix_player.h"

APA102    leds(LED_DATA_PIN, LED_CLK_PIN, NUM_LEDS);
PixPlayer player(leds);

void setup() {
    Serial.begin(115200);

    if (!leds.begin(20000000)) {
        Serial.println("[apa102] init failed");
        while (true);
    }
    leds.clear();
    leds.show();

    if (LittleFS.exists(PIX_FILE)) {
        int err = player.load(PIX_FILE);
        if (err) { Serial.printf("[pix] load failed: %d\n", err); return; }
        player.startTask(1);  // core 1, priorita 5
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // loop() nemá co dělat
}
