# AuraX

ESP32-S3 firmware přehrávající `.pix` animace z LittleFS na LED pásek **APA102** (SPI+DMA) nebo **WS281x** (RMT) — volba přes `LED_TYPE` v `src/config.h`. Konfigurace za běhu přes web UI, přístupné na `aurax.local`.

## Quick Reference

```bash
pio run                   # kompilace
pio run -t upload         # flash firmware
pio run -t uploadfs       # flash LittleFS (data/ složka) — spustit první na čisté desce
pio device monitor        # sériový monitor (jen s #define PIX_DEBUG)
```

Výchozí konfigurace: `src/config.h` (LED typ, piny, počet LED, PIX_FILE). Za běhu přepisuje `/config.json` v LittleFS — editovatelné přes web UI nebo `POST /config`.

## Architecture

- **`ILedDriver`** — abstraktní interface (`src/led_driver.h`): `showColumnDirect()` + `numLeds()` + `clear()`. Implementují APA102 a WS281x.
- **`APA102`** — SPI driver s DMA double bufferingem. `showAsync()` zařadí DMA a vrátí se; čekání na předchozí přenos proběhne až na začátku *dalšího* volání → CPU a DMA se překrývají.
- **`WS281x`** — RMT driver (ESP-IDF v4 API, `driver/rmt.h`). Double-buffer async stejný pattern jako APA102. Pixel encoding: `.pix` `[0xE0|bri, B, G, R]` → brightness scaling → GRB bity přes RMT.
- **`PixPlayer`** — parsuje `.pix` soubory, načítá obrazová data do PSRAM (nebo streamuje z LittleFS jako fallback), zobrazuje sloupce časovaně přes `esp_timer_get_time()`. Běží jako FreeRTOS task na **core 1, priorita 5**; `loop()` parkuje na `vTaskDelay(portMAX_DELAY)`.
- **`WifiControl`** — HTTP server (port 80) na core 0, priorita 2. Připojuje se k WiFi STA (2 pokusy), fallback na soft AP `AuraX-XXXX`. mDNS (`aurax.local`), UDP discovery (port 4210), REST API.
- **`SyncControl`** — ESP-NOW broadcast synchronizace. Tři příkazy: `CMD_PLAY` (sync start s `endBehavior` a `delayMs`), `CMD_STOP` (zastaví animaci i efekt), `CMD_EFFECT` (sync efekt). `broadcastPlay/Stop/Effect()` odešle packet všem peerům a provede akci i lokálně. Příjem v ISR → fronta → `process()` v `wifi_ctrl` tasku.
- **`AppConfig`** — runtime konfigurace načítaná z `/config.json` (LittleFS). Fallback na compile-time hodnoty z `src/config.h`.
- **Rychlostní strop APA102** (20 MHz SPI): ~3 600 řádků/s pro 170 LED, ~4 200 pro 144 LED.
- **Rychlostní strop WS281x** (800 kHz): ~230 řádků/s pro 144 LED — protokol neumožňuje víc.
- **Hot path APA102:** pixely v `.pix` jsou `[0xE0|brightness, B, G, R]` = přesně APA102 drátový formát → `showColumnDirect()` dělá jen `memcpy` do DMA bufferu. `bri=0` (0xE0) se mapuje na 20% jas (0xE6).
- **Hot path WS281x:** `encodePixels()` aplikuje brightness a konvertuje do 24 RMT položek na LED (GRB, MSB first).

Detaily: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Formát souborů: [docs/pix-format.md](docs/pix-format.md)

## Web API

| Endpoint | Metoda | Popis |
|---|---|---|
| `/` | GET | Web UI |
| `/play` | GET | Spustit animaci (nebo broadcastovat přes ESP-NOW) |
| `/stop` | GET | Zastavit animaci |
| `/off` | GET | Blackout — zastavit + zhasnout LED |
| `/status` | GET | JSON stav (playing, file, frames_rendered, frames_expected, ip, hostname, battery, ap_mode) |
| `/peers` | GET | JSON seznam zařízení v síti |
| `/config` | GET/POST | Číst/zapsat konfiguraci (JSON) |
| `/brightness` | GET | Nastavit jas LED (`?v=0..100`) |
| `/tempo` | GET | Nastavit rychlost přehrávání (`?v=10..1000`, 100 = normální) |
| `/endbehavior` | GET | Přepsat chování konce show (`?v=0..2` nebo `255` = ze souboru) |
| `/upload` | POST | Nahrát `.pix` soubor (multipart) |
| `/update` | POST | OTA aktualizace firmware (multipart `.bin`) |
| `/reboot` | POST | Restart zařízení |

## Multi-device (mDNS + UDP discovery)

- Každé zařízení se přihlásí jako `aurax.local` (výchozí hostname).
- **Konflikt:** pokud jsou v síti dvě zařízení se stejným hostname, přejmenuje se to s vyšším chip ID na `aurax-XXXX.local`. Přejmenování je jen v paměti — po vypnutí vítěze si druhé zařízení `aurax.local` automaticky přivlastní zpět (po 90 s bez aktivity peera).
- **UDP discovery** (port 4210): každé zařízení broadcastuje `AURAX <hostname> <ip> <chipid>` při startu a každých 30 s. Peeri jsou viditelní v `/peers` a ve web UI.
- **AP fallback:** při selhání WiFi (2 pokusy × timeout) zařízení spustí soft AP `AuraX-XXXX` — web UI dostupné na `aurax.local` (192.168.4.1).
- **Crash recovery:** `esp_reset_reason()` detekuje panic/watchdog reset → autoplay se přeskočí, zařízení startuje zastavené.

## Conventions

- PSRAM alokace: `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
- DMA alokace: `heap_caps_malloc(..., MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`
- `goto streaming` v `PixPlayer::load()` je záměrné — fallback cesta při selhání PSRAM alokace.
- Interní pixel formát APA102: `[R, G, B, brightness]`; drátový formát: `[0xE0|bri, B, G, R]`.
- Debug výpisy: makra `LOG(...)` / `LOGLN(s)` definovaná v `config.h` — aktivní jen při `#define PIX_DEBUG`. `Serial.begin()` se volá jen tehdy.
- Formátování: `.clang-format` v rootu projektu (Google style, indent 4). Spouští se automaticky přes PostToolUse hook po každém Edit/Write — ruční volání není třeba.

## Build Environment

> **VAROVÁNÍ: nespouštěj `pio upgrade`** bez otestování — SPI API se mezi verzemi `espressif32` mění a může tiše rozbít build.

- Platform: `espressif32@5.3.0` — verze je záměrně zmrazená v `platformio.ini`.
- Board: `esp32-s3-devkitc-1`, `memory_type = qio_opi` (vyžaduje 8MB OPI PSRAM).
- Filesystem: LittleFS — partition label je `spiffs` (správně, PlatformIO to tak mapuje).
- Závislosti: `bblanchon/ArduinoJson @ ^6.21` (runtime config).

## Workflow

- Před úpravou SPI/DMA/partition konfigurace **napiš plán a počkej na schválení** — špatná konfigurace může vyžadovat erase flash.
- `uploadfs` a `upload` jsou separátní operace. Na čisté desce: nejdřív `uploadfs`, pak `upload`.
- Po opravě chyby nebo zjištění neočekávané vlastnosti: navrhni přidání do sekce Learnings.

## Verification

Před flashováním — ověř, že firmware jde zkompilovat:
```bash
pio run
```

Po flashování — pro ověření zapni `#define PIX_DEBUG` a sleduj sériový monitor:
```
[cfg] ledType=... numLeds=... file=/show.pix
[wifi] connecting to MySSID (pokus 1/2)....
[wifi] connected, IP: 192.168.1.X
[mdns] http://aurax.local
[pix] preloaded XXXXXX bytes into PSRAM, N commands
```
Pokud LEDky zobrazují animaci a `http://aurax.local` otevře web UI, ověření je hotové.

| Výstup | Příčina | Řešení |
|---|---|---|
| `[pix] streaming mode` | PSRAM nedostupná | zkontroluj `memory_type = qio_opi` v platformio.ini |
| `[apa102] init failed` | SPI bus nelze inicializovat | zkontroluj GPIO piny v `src/config.h` |
| `[ws281x] init failed` | RMT nelze inicializovat | zkontroluj `WS_DATA_PIN` v `src/config.h` |
| `[pix] load failed: 5` | soubor nenalezen v LittleFS | spusť `pio run -t uploadfs` |
| `[pix] load failed: 3` | LittleFS se nepodařilo připojit | zkontroluj partition tabulku |
| žádný výstup na monitoru | PIX_DEBUG není definováno | přidej `#define PIX_DEBUG` do `config.h` |
| `AP mode: SSID=AuraX-XXXX` | WiFi se nepřipojilo | zkontroluj SSID/heslo v nastavení nebo přijď na `aurax.local` přes AP |
| `[sys] crash detected` | předchozí běh skončil pádem | zařízení startuje zastavené — spusť ručně přes web UI |

## Gotchas

- **PSRAM je nutná pro plný výkon.** Bez ní přehrávač fallbackne na streaming (LittleFS seek na každý snímek) — výrazně pomalejší.
- **Serial výstup je podmíněný** — bez `#define PIX_DEBUG` se `Serial.begin()` vůbec nevolá. UART piny nedají žádný výstup i při USB-CDC.
- **GPIO5/6 procházejí GPIO matrix** (ne nativní SPI). Při 20 MHz zanedbatelné. Nativní SPI piny XIAO: `GPIO9` (D10) a `GPIO7` (D8).
- **DMA buffer musí být 4-byte zarovnaný** — `_txLen` se zarovnává v `begin()` přes `(_txLen + 3) & ~3u`.
- **WS281x brightness:** `bri=0` v `.pix` souboru = plný jas (WS281x soubory). `bri=1–31` = APA102-style škálování. APA102 mapuje `bri=0` na 20% (0xE6).
- **WS281x je protokolově omezen na ~230 řádků/s** pro 144 LED (800 kHz × 24 bit/LED = 4,3 ms/snímek).
- **UDP discovery nefunguje v AP módu** — broadcast síť neexistuje, peer discovery je vypnuté.
- **mDNS konflikt**: chip ID je jen 16 bitů — při kolizi (nepravděpodobné) se přejmenuje první příchozí.
- **syncCtrl.begin() musí být voláno až po wifi.begin()** — ESP-NOW potřebuje inicializovaný WiFi stack a správný kanál.

## Learnings

- `loop()` na ESP32 běží jako FreeRTOS task s prioritou 1 — nestačí pro přesné časování nad ~1000 Hz. Řešení: dedikovaný task s vyšší prioritou.
- `vTaskDelay(pdMS_TO_TICKS(1))` při tick rate 1 kHz uspí na celou 1 ms — při frame intervalu 1 ms zablokuje přehrávání. Řešení: spin-loop pro intervaly < 10 ms.
- FreeRTOS `loopTask` je sdílený s Arduino frameworkem — jiné knihovny (WiFi, BLE) ho mohou zdržet. Přehrávač musí běžet v separátním tasku.
- **⚠ CRASH: ESP-NOW receive callback nesmí volat `vTaskDelay()` ani LittleFS** — způsobí crash. Řešení: `xQueueSendFromISR()` v callbacku, zpracování v samostatném tasku.
- `wifi_ctrl` task potřebuje stack ≥ 8192 bytů — `handleConfigPost` + `saveConfig` alokují dva `StaticJsonDocument<512>` na stacku.
- **⚠ HANG: `xTaskCreatePinnedToCore` pro wifi_ctrl musí být voláno PŘED `player->startTask()`** — pix_player (Core 1, priorita 5) v spin-loop nikdy neuvolní Core 1 nižší prioritě, takže `setup()` by se za `startTask()` nikdy nedostalo. wifi_ctrl je na Core 0, takže mu to nevadí, ale musí být vytvořen dřív.
- **`.pix` soubory ukládají `startTime` jako signed int32** — záporná hodnota znamená "začni před t=0" (tj. okamžitě). Parser čte uint32, takže -2000 ms se stane 4 294 965 296 ms ≈ 49 dní → zařízení vypadá zmrzlé. Řešení: `((int32_t)val < 0) ? 0 : val`.
- **`handlePlay()` volá `scheduleStart(now + 20 ms)` před `startTask()`** — bez toho se `_nextFrameUs` nastaví na Core 0, ale task nastartuje na Core 1 až za ~100–500 µs; za tu dobu přehrávač vyhodnotí první snímky jako zmeškané a čítač `framesExpected` okamžitě skočí. 20 ms rezerva je dost pro spuštění tasku a zanedbatelná pro uživatele.
- **`PixPlayer::Stats::framesExpected` se odvozuje z pozice v animaci**, nikoli z časovače — `loopCount × totalFrames + součet dokončených příkazů + curCol`. Předchozí přístup (akumulace `(now − _nextFrameUs) / interval`) dával pokaždé jiný výsledek kvůli FreeRTOS jitteru.
