#include <Arduino.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
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
static bool          fsMounted    = false;

class NullLedDriver : public ILedDriver {
public:
    explicit NullLedDriver(uint16_t count) : _count(count ? count : 1) {}
    uint16_t numLeds() const override { return _count; }
    void showColumnDirect(const uint8_t*, uint16_t) override {}
    void clear() override {}
private:
    uint16_t _count;
};

static ILedDriver* createLedDriver(AppConfig& cfg) {
    if (cfg.ledType == LED_TYPE_APA102) {
        auto* d = new APA102(cfg.dataPin, cfg.clkPin, cfg.numLeds);
        if (d->begin(20000000)) {
            d->clear();
            d->show();
            return d;
        }
        LOGLN("[apa102] init failed, trying WS281x fallback");
    }

    auto* ws = new WS281x(cfg.dataPin, cfg.numLeds);
    if (ws->begin()) {
        cfg.ledType = LED_TYPE_WS281X;
        return ws;
    }
    LOGLN("[ws281x] init failed on configured pin, trying default WS pin");

    auto* wsDefault = new WS281x(WS_DATA_PIN, cfg.numLeds);
    if (wsDefault->begin()) {
        cfg.ledType = LED_TYPE_WS281X;
        cfg.dataPin = WS_DATA_PIN;
        return wsDefault;
    }

    LOGLN("[led] all drivers failed, starting WiFi with null LED driver");
    return new NullLedDriver(cfg.numLeds);
}

void setup() {
#ifdef PIX_DEBUG
    Serial.begin(115200);
    delay(1500);
#endif

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (runningPartition &&
        esp_ota_get_state_partition(runningPartition, &otaState) == ESP_OK &&
        otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            LOGLN("[ota] marked app valid");
        }
    }

    fsMounted = LittleFS.begin(false);
    if (!fsMounted) {
        LOGLN("[fs] LittleFS mount failed, continuing with AP fallback");
    }

    cfg = fsMounted ? loadConfig() : defaultConfig();
    LOG("[cfg] ledType=%d numLeds=%d dataPin=%d clkPin=%d file=%s\n",
        cfg.ledType, cfg.numLeds, cfg.dataPin, cfg.clkPin, cfg.pixFile);

    leds = createLedDriver(cfg);

    leds->setBrightness(cfg.brightness);
    leds->setReverse(cfg.effectReverse != 0);
    leds->setMirror(cfg.renderMirror != 0);
    leds->setCurrentLimit(cfg.mALimit, 60);

    player       = new PixPlayer(*leds);
    player->setTempo(cfg.tempo);
    player->setEndBehavior(cfg.endBehavior);
    effectPlayer = new EffectPlayer(*leds);
    syncCtrl     = new SyncControl(*player, *effectPlayer, *leds);
    wifi         = new WifiControl(*player, *effectPlayer, *leds, cfg, syncCtrl, fsMounted);

    // wifi_ctrl musí být vytvořen PŘED player->startTask() — pix_player běží
    // na Core 1 s prioritou 5 a při spin-loop blokuje loopTask (taky Core 1,
    // priorita 1), takže by se řádek za startTask() nikdy nevykonal.
    xTaskCreatePinnedToCore(
        [](void*) {
            wifi->begin();
            LOGLN("[wifi] server ready");
            if (fsMounted && configNeedsSave()) {
                vTaskDelay(pdMS_TO_TICKS(1500));
                bool saved = saveConfig(cfg);
                LOG("[cfg] delayed config save: %s\n", saved ? "OK" : "FAILED");
            }
            if (cfg.syncEnabled && cfg.syncMask) {
                syncCtrl->begin(cfg.syncMask, cfg.syncEnabled);
            } else {
                LOGLN("[sync] disabled");
            }
            while (true) { syncCtrl->process(); wifi->handle(); vTaskDelay(1); }
        },
        "wifi_ctrl", 8192, nullptr, 2, nullptr, 0  // core 0, priorita 2
    );

    esp_reset_reason_t resetReason = esp_reset_reason();
    bool crashed = (resetReason == ESP_RST_PANIC    ||
                    resetReason == ESP_RST_INT_WDT  ||
                    resetReason == ESP_RST_TASK_WDT ||
                    resetReason == ESP_RST_BROWNOUT ||
                    resetReason == ESP_RST_WDT);
    if (crashed) {
        LOG("[sys] crash detected (reason=%d), autoplay disabled\n", (int)resetReason);
    } else if (cfg.autoStart == 1) {
        EffectParams p = {};
        p.effectId    = cfg.effectId;
        p.speed       = cfg.effectSpeed;
        p.intensity   = cfg.effectIntensity;
        p.dotSize     = cfg.effectDotSize;
        p.paletteId   = cfg.effectPaletteId;
        p.paletteSize = cfg.paletteSize;
        p.reverse     = cfg.effectReverse;
        for (int i = 0; i < cfg.paletteSize && i < 4; i++)
            p.palette[i] = { cfg.paletteR[i], cfg.paletteG[i], cfg.paletteB[i] };
        if (p.paletteSize == 0) { p.palette[0] = {255, 0, 0}; p.paletteSize = 1; }
        effectPlayer->start(p);
        LOG("[sys] restored effect id=%d\n", p.effectId);
    } else if (fsMounted && LittleFS.exists(cfg.pixFile)) {
        int err = player->load(cfg.pixFile);
        if (err) { LOG("[pix] load failed: %d\n", err); }
        else player->startTask(1);
    } else if (!fsMounted) {
        LOGLN("[pix] storage unavailable, autoplay skipped");
    } else {
        LOG("[pix] soubor nenalezen: %s\n", cfg.pixFile);
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
