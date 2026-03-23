# AuraX

ESP32-S3 firmware přehrávající `.pix` animace z LittleFS na LED pásek APA102 přes hardware SPI s DMA.

## Quick Reference

```bash
pio run                   # kompilace
pio run -t upload         # flash firmware
pio run -t uploadfs       # flash LittleFS (data/ složka) — spustit první na čisté desce
pio device monitor        # sériový monitor, 115200 baud
```

Piny (testovací deska): `DATA=GPIO6`, `CLK=GPIO5` — definice v `main.cpp`.
LED soubor: umístit jako `data/test.pix`, cesta předána do `player.load("/test.pix")`.

## Architecture

- **`APA102`** — SPI driver s DMA double bufferingem. `showAsync()` zařadí DMA a vrátí se; čekání na předchozí přenos proběhne až na začátku *dalšího* volání → CPU a DMA se překrývají.
- **`PixPlayer`** — parsuje `.pix` soubory, načítá obrazová data do PSRAM (nebo streamuje z LittleFS jako fallback), zobrazuje sloupce časovaně přes `esp_timer_get_time()`. Běží jako FreeRTOS task na **core 1, priorita 5**; `loop()` parkuje na `vTaskDelay(portMAX_DELAY)`.
- **Rychlostní strop** (20 MHz SPI): ~3 600 řádků/s pro 170 LED, ~4 200 pro 144 LED. Dosažitelné 1000+ řádků/s díky spin-loop timingu (nezávislé na FreeRTOS tick rate).
- **Hot path:** pixely v `.pix` jsou `[0xE0|brightness, B, G, R]` = přesně APA102 drátový formát → `showColumnDirect()` dělá jen `memcpy` do DMA bufferu.

Detaily: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Formát souborů: [docs/pix-format.md](docs/pix-format.md)

## Build Environment

- Platform: `espressif32@5.3.0` — **nespouštěj `pio upgrade`** bez otestování; SPI API se mezi verzemi mění.
- Board: `esp32-s3-devkitc-1`, `memory_type = qio_opi` (vyžaduje 8MB OPI PSRAM).
- Filesystem: LittleFS — partition label je `spiffs` (správně, PlatformIO to tak mapuje).

## Conventions

- PSRAM alokace: `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
- DMA alokace: `heap_caps_malloc(..., MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`
- `goto streaming` v `PixPlayer::load()` je záměrné — fallback cesta při selhání PSRAM alokace.
- Interní pixel formát APA102: `[R, G, B, brightness]`; drátový formát: `[0xE0|bri, B, G, R]`.

## Verification

Po flashování — očekávaný výstup na sériovém monitoru:
```
[pix] preloaded XXXXXX bytes into PSRAM, N commands
```

| Výstup | Příčina | Řešení |
|---|---|---|
| `[pix] streaming mode` | PSRAM nedostupná | zkontroluj `memory_type = qio_opi` v platformio.ini |
| `[apa102] init failed` | SPI bus nelze inicializovat | zkontroluj GPIO piny v main.cpp |
| `[pix] load failed: 5` | soubor nenalezen v LittleFS | spusť `pio run -t uploadfs` |
| `[pix] load failed: 3` | LittleFS se nepodařilo připojit | zkontroluj partition tabulku |
| žádný výstup | USB-CDC není aktivní | připojit přes USB-C, ne UART piny |

## Gotchas

- **PSRAM je nutná pro plný výkon.** Bez ní přehrávač fallbackne na streaming (LittleFS seek na každý snímek) — výrazně pomalejší.
- **Serial funguje jen přes USB-CDC** (`ARDUINO_USB_CDC_ON_BOOT=1` je nastaveno). UART piny nedají žádný výstup.
- **GPIO5/6 procházejí GPIO matrix** (ne nativní SPI). Při 20 MHz zanedbatelné. Nativní SPI piny XIAO: `GPIO9` (D10) a `GPIO7` (D8).
- **DMA buffer musí být 4-byte zarovnaný** — `_txLen` se zarovnává v `begin()` přes `(_txLen + 3) & ~3u`.
- **`uploadfs` a `upload` jsou separátní operace** — obě jsou nutné na čisté desce.

## Learnings

<!-- Po každé opravě: "Přidej to do CLAUDE.md ať tu chybu neuděláš znovu." -->
