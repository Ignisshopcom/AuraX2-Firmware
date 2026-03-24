#include <Arduino.h>
#include "app_config.h"
#include "config.h"
#include "led_driver.h"
#include "pix_player.h"
#include "wifi_control.h"
#include "sync_control.h"

#include "apa102.h"
#include "ws281x.h"

static AppConfig    cfg;
static ILedDriver*  leds     = nullptr;
static PixPlayer*   player   = nullptr;
static SyncControl* syncCtrl = nullptr;
static WifiControl* wifi     = nullptr;

void setup() {
    Serial.begin(115200);
#ifdef PIX_DEBUG
    delay(1500);
#endif

    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed");
        return;
    }

    cfg = loadConfig();
    Serial.printf("[cfg] ledType=%d numLeds=%d dataPin=%d clkPin=%d file=%s\n",
        cfg.ledType, cfg.numLeds, cfg.dataPin, cfg.clkPin, cfg.pixFile);

    // Instantiate LED driver based on runtime config
    if (cfg.ledType == LED_TYPE_APA102) {
        auto* d = new APA102(cfg.dataPin, cfg.clkPin, cfg.numLeds);
        if (!d->begin(20000000)) { Serial.println("[apa102] init failed"); while (true); }
        d->clear(); d->show();
        leds = d;
    } else {
        auto* d = new WS281x(cfg.dataPin, cfg.numLeds);
        if (!d->begin()) { Serial.println("[ws281x] init failed"); while (true); }
        leds = d;
    }

    player   = new PixPlayer(*leds);
    syncCtrl = new SyncControl(*player);
    wifi     = new WifiControl(*player, cfg, syncCtrl);

    if (LittleFS.exists(cfg.pixFile)) {
        int err = player->load(cfg.pixFile);
        if (err) { Serial.printf("[pix] load failed: %d\n", err); }
        else player->startTask(1);
    } else {
        Serial.printf("[pix] soubor nenalezen: %s\n", cfg.pixFile);
    }

    xTaskCreatePinnedToCore(
        [](void*) {
            if (wifi->begin())
                Serial.println("[wifi] server ready");
            else
                Serial.println("[wifi] offline — server not started");
            syncCtrl->begin();
            while (true) { syncCtrl->process(); wifi->handle(); vTaskDelay(1); }
        },
        "wifi_ctrl", 4096, nullptr, 2, nullptr, 0  // core 0, priorita 2
    );
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
