# AuraX

ESP32-S3 firmware přehrávající `.pix` animace z LittleFS na LED pásek **APA102** (SPI+DMA) nebo **WS281x** (RMT) — volba přes `LED_TYPE` v `src/config.h`.

## Quick Reference

```bash
pio run                   # kompilace
pio run -t upload         # flash firmware
pio run -t uploadfs       # flash LittleFS (data/ složka) — spustit první na čisté desce
pio device monitor        # sériový monitor, 115200 baud
```

Typ pásku a piny: `src/config.h` (`LED_TYPE`, `LED_DATA_PIN=6`, `LED_CLK_PIN=5` pro APA102; `WS_DATA_PIN=6` pro WS281x; `NUM_LEDS=44`).
LED soubor: umístit jako `data/show.pix` — cesta definována jako `PIX_FILE` v `src/config.h`.

## Architecture

- **`ILedDriver`** — abstraktní interface (`src/led_driver.h`): `showColumnDirect()` + `numLeds()`. Implementují APA102 a WS281x.
- **`APA102`** — SPI driver s DMA double bufferingem. `showAsync()` zařadí DMA a vrátí se; čekání na předchozí přenos proběhne až na začátku *dalšího* volání → CPU a DMA se překrývají.
- **`WS281x`** — RMT driver (ESP-IDF v4 API, `driver/rmt.h`). Double-buffer async stejný pattern jako APA102. Pixel encoding: `.pix` `[0xE0|bri, B, G, R]` → brightness scaling → GRB bity přes RMT.
- **`PixPlayer`** — parsuje `.pix` soubory, načítá obrazová data do PSRAM (nebo streamuje z LittleFS jako fallback), zobrazuje sloupce časovaně přes `esp_timer_get_time()`. Běží jako FreeRTOS task na **core 1, priorita 5**; `loop()` parkuje na `vTaskDelay(portMAX_DELAY)`.
- **Rychlostní strop APA102** (20 MHz SPI): ~3 600 řádků/s pro 170 LED, ~4 200 pro 144 LED. 1000+ řádků/s díky spin-loop timingu.
- **Rychlostní strop WS281x** (800 kHz): ~230 řádků/s pro 144 LED — protokol neumožňuje víc.
- **Hot path APA102:** pixely v `.pix` jsou `[0xE0|brightness, B, G, R]` = přesně APA102 drátový formát → `showColumnDirect()` dělá jen `memcpy` do DMA bufferu.
- **Hot path WS281x:** `encodePixels()` aplikuje brightness a konvertuje do 24 RMT položek na LED (GRB, MSB first).

Detaily: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Formát souborů: [docs/pix-format.md](docs/pix-format.md)

## Conventions

- PSRAM alokace: `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
- DMA alokace: `heap_caps_malloc(..., MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`
- `goto streaming` v `PixPlayer::load()` je záměrné — fallback cesta při selhání PSRAM alokace.
- Interní pixel formát APA102: `[R, G, B, brightness]`; drátový formát: `[0xE0|bri, B, G, R]`.

## Build Environment

> **VAROVÁNÍ: nespouštěj `pio upgrade`** bez otestování — SPI API se mezi verzemi `espressif32` mění a může tiše rozbít build.

- Platform: `espressif32@5.3.0` — verze je záměrně zmrazená v `platformio.ini`.
- Board: `esp32-s3-devkitc-1`, `memory_type = qio_opi` (vyžaduje 8MB OPI PSRAM).
- Filesystem: LittleFS — partition label je `spiffs` (správně, PlatformIO to tak mapuje).

## Workflow

- Před úpravou SPI/DMA/partition konfigurace **napiš plán a počkej na schválení** — špatná konfigurace může vyžadovat erase flash.
- `uploadfs` a `upload` jsou separátní operace. Na čisté desce: nejdřív `uploadfs`, pak `upload`.
- Po opravě chyby nebo zjištění neočekávané vlastnosti: navrhni přidání do sekce Learnings.

## Verification

Po flashování — očekávaný výstup na sériovém monitoru:
```
[pix] preloaded XXXXXX bytes into PSRAM, N commands
```
Pokud LEDky zobrazují animaci, ověření je hotové.

| Výstup | Příčina | Řešení |
|---|---|---|
| `[pix] streaming mode` | PSRAM nedostupná | zkontroluj `memory_type = qio_opi` v platformio.ini |
| `[apa102] init failed` | SPI bus nelze inicializovat | zkontroluj GPIO piny v `src/config.h` |
| `[ws281x] init failed` | RMT nelze inicializovat | zkontroluj `WS_DATA_PIN` v `src/config.h` |
| `[pix] load failed: 5` | soubor nenalezen v LittleFS | spusť `pio run -t uploadfs` |
| `[pix] load failed: 3` | LittleFS se nepodařilo připojit | zkontroluj partition tabulku |
| žádný výstup | USB-CDC není aktivní | připojit přes USB-C, ne UART piny |

## Gotchas

- **PSRAM je nutná pro plný výkon.** Bez ní přehrávač fallbackne na streaming (LittleFS seek na každý snímek) — výrazně pomalejší.
- **Serial funguje jen přes USB-CDC** (`ARDUINO_USB_CDC_ON_BOOT=1` je nastaveno). UART piny nedají žádný výstup.
- **GPIO5/6 procházejí GPIO matrix** (ne nativní SPI). Při 20 MHz zanedbatelné. Nativní SPI piny XIAO: `GPIO9` (D10) a `GPIO7` (D8).
- **DMA buffer musí být 4-byte zarovnaný** — `_txLen` se zarovnává v `begin()` přes `(_txLen + 3) & ~3u`.
- **170 LED** ve Quick Reference je příklad výpočtu rychlosti, ne aktuálně nakonfigurovaný počet (ten je 144 v `src/config.h`).
- **WS281x brightness:** `bri=0` v `.pix` souboru = brightness není použit (soubory pro WS281x/NeoPixel) → driver použije plný jas. `bri=1–31` = APA102-style škálování.
- **WS281x je protokolově omezen na ~230 řádků/s** pro 144 LED (800 kHz × 24 bit/LED = 4,3 ms/snímek). Spin-loop timing ani vyšší priorita task to nezmění.

## Learnings

- `loop()` na ESP32 běží jako FreeRTOS task s prioritou 1 — nestačí pro přesné časování nad ~1000 Hz. Řešení: dedikovaný task s vyšší prioritou.
- `vTaskDelay(pdMS_TO_TICKS(1))` při tick rate 1 kHz uspí na celou 1 ms — při frame intervalu 1 ms zablokuje přehrávání. Řešení: spin-loop pro intervaly < 10 ms.
- FreeRTOS `loopTask` je sdílený s Arduino frameworkem — jiné knihovny (WiFi, BLE) ho mohou zdržet. Přehrávač musí běžet v separátním tasku.
