#include <Arduino.h>
#include <esp_system.h>
#include "app_config.h"
#include "config.h"
#include "led_driver.h"
#include "pix_player.h"
#include "effect_player.h"
#include "wifi_control.h"
#include "sync_control.h"

#include "apa102.h"
#include "ws281x.h"

static AppConfig     cfg;
static ILedDriver*   leds         = nullptr;
static PixPlayer*    player       = nullptr;
static EffectPlayer* effectPlayer = nullptr;
static SyncControl*  syncCtrl     = nullptr;
static WifiControl*  wifi         = nullptr;

void setup() {
#ifdef PIX_DEBUG
    Serial.begin(115200);
    delay(1500);
#endif

    if (!LittleFS.begin(true)) {
        LOGLN("[fs] LittleFS mount failed");
        return;
    }

    cfg = loadConfig();
    LOG("[cfg] ledType=%d numLeds=%d dataPin=%d clkPin=%d file=%s\n",
        cfg.ledType, cfg.numLeds, cfg.dataPin, cfg.clkPin, cfg.pixFile);

    // Instantiate LED driver based on runtime config
    if (cfg.ledType == LED_TYPE_APA102) {
        auto* d = new APA102(cfg.dataPin, cfg.clkPin, cfg.numLeds);
        if (!d->begin(20000000)) { LOGLN("[apa102] init failed"); while (true); }
        d->clear(); d->show();
        leds = d;
    } else {
        auto* d = new WS281x(cfg.dataPin, cfg.numLeds);
        if (!d->begin()) { LOGLN("[ws281x] init failed"); while (true); }
        leds = d;
    }

    leds->setBrightness(cfg.brightness);
    leds->setCurrentLimit(cfg.mALimit, 60);

    player       = new PixPlayer(*leds);
    player->setTempo(cfg.tempo);
    player->setEndBehavior(cfg.endBehavior);
    effectPlayer = new EffectPlayer(*leds);
    syncCtrl     = new SyncControl(*player, *effectPlayer);
    wifi         = new WifiControl(*player, *effectPlayer, *leds, cfg, syncCtrl);

    // wifi_ctrl musí být vytvořen PŘED player->startTask() — pix_player běží
    // na Core 1 s prioritou 5 a při spin-loop blokuje loopTask (taky Core 1,
    // priorita 1), takže by se řádek za startTask() nikdy nevykonal.
    xTaskCreatePinnedToCore(
        [](void*) {
            wifi->begin();
            LOGLN("[wifi] server ready");
            syncCtrl->begin(cfg.syncChannel);
            while (true) { syncCtrl->process(); wifi->handle(); vTaskDelay(1); }
        },
        "wifi_ctrl", 8192, nullptr, 2, nullptr, 0  // core 0, priorita 2
    );

    esp_reset_reason_t resetReason = esp_reset_reason();
    bool crashed = (resetReason == ESP_RST_PANIC    ||
                    resetReason == ESP_RST_INT_WDT  ||
                    resetReason == ESP_RST_TASK_WDT ||
                    resetReason == ESP_RST_WDT);
    if (crashed) {
        LOG("[sys] crash detected (reason=%d), autoplay disabled\n", (int)resetReason);
    } else if (cfg.autoStart == 1) {
        EffectParams p = {};
        p.effectId    = cfg.effectId;
        p.speed       = cfg.effectSpeed;
        p.dotSize     = cfg.effectDotSize;
        p.paletteSize = cfg.paletteSize;
        for (int i = 0; i < cfg.paletteSize && i < 4; i++)
            p.palette[i] = { cfg.paletteR[i], cfg.paletteG[i], cfg.paletteB[i] };
        if (p.paletteSize == 0) { p.palette[0] = {255, 0, 0}; p.paletteSize = 1; }
        effectPlayer->start(p);
        LOG("[sys] restored effect id=%d\n", p.effectId);
    } else if (LittleFS.exists(cfg.pixFile)) {
        int err = player->load(cfg.pixFile);
        if (err) { LOG("[pix] load failed: %d\n", err); }
        else player->startTask(1);
    } else {
        LOG("[pix] soubor nenalezen: %s\n", cfg.pixFile);
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
