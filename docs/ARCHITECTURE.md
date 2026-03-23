# AuraX — architektura firmware

## Přehled

Firmware pro ESP32-S3 (Seeed Studio XIAO ESP32S3) přehrávající animace ve formátu `.pix` na pásku LED APA102.

---

## Hardware

| Parametr | Hodnota |
|---|---|
| MCU | ESP32-S3 |
| Board | Seeed Studio XIAO ESP32S3 |
| Flash | 8 MB (QIO, 80 MHz) |
| PSRAM | 8 MB OPI |
| LED protokol | APA102 (SPI: data + clock) |
| Výchozí piny (testovací deska) | DATA = GPIO6, CLK = GPIO5 |
| Výchozí piny (XIAO header) | DATA = GPIO9 (D10), CLK = GPIO7 (D8) |

Piny GPIO6/5 procházejí GPIO matrix (ne nativní SPI linka), rozdíl výkonu je zanedbatelný.

---

## Moduly

### `apa102.h / apa102.cpp` — driver APA102

Řídí LED pásek přes hardware SPI s DMA přenosem.

**Klíčové vlastnosti:**
- ESP-IDF `spi_master` driver s `SPI_DMA_CH_AUTO`
- Double buffering: DMA posílá snímek N, CPU připravuje snímek N+1
- `showAsync()` — zařadí DMA přenos do fronty a okamžitě se vrátí; čekání na dokončení předchozího přenosu probíhá na začátku *dalšího* volání
- `showColumnDirect()` — přímá cesta pro pix player: `memcpy` raw dat do DMA bufferu bez konverze

**Buffer layout:**
```
[4 B start frame 0x00] [N × 4 B LED data] [ceil(N/16) B end frame 0xFF]
```

**Pixel formát (interní):** `[R, G, B, brightness]` per LED
**Pixel formát na drátě:** `[0xE0|brightness, B, G, R]` per LED — konverzi dělá `buildTxBuffer()`

**API:**
```cpp
APA102 leds(dataPin, clkPin, numLeds, host = SPI2_HOST);
leds.begin(freqHz = 20000000);         // inicializace
leds.setPixel(i, r, g, b, bri = 31);  // brightness 0–31
leds.fill(r, g, b, bri = 31);
leds.clear();
leds.show();                           // blocking
leds.showAsync();                      // non-blocking, DMA
leds.waitForShow();                    // čeká na dokončení DMA
leds.showColumnDirect(ptr, count);     // přímý DMA z pix bufferu
leds.numLeds();                        // počet LED
```

---

### `pix_player.h / pix_player.cpp` — přehrávač .pix souborů

Parsuje `.pix` soubory z LittleFS a řídí APA102.

**Módy načítání:**
1. **Preload (PSRAM)** — celý obrazový obsah se načte do PSRAM (`MALLOC_CAP_SPIRAM`). Při 144 LED × 1000 sloupců ≈ 576 KB; 8MB PSRAM to bez problémů pojme. Čtení sloupce = pointer aritmetika, žádné I/O.
2. **Streaming (fallback)** — soubor zůstane otevřený, každý sloupec se načte seekem + `File.read()`. Použije se pokud PSRAM nestačí.

**Časování:** `esp_timer_get_time()` (mikrosekundová přesnost). Interval snímku = `1 000 000 / frequency` µs.

**Multi-command:** program může obsahovat více `picture_command` záznamů; přehrávač je postupně prochází. Po posledním se chování řídí `progEndBehavior` (Repeat / Keep / Exit).

**Provozní módy:**
- `player.update()` — volat z `loop()` co nejrychleji; vrátí `false` při konci přehrávání (Exit)
- `player.startTask(core)` — spustí FreeRTOS task na zadaném jádře

**Přímá cesta (hot path):**
Pixel v `.pix` souboru má formát `[0xE0, B, G, R]` = přesně APA102 drátový formát → `showColumnDirect()` dělá jen `memcpy` do DMA bufferu, žádná konverze barev.

**`PixEndBehavior` enum:**
```cpp
PixEndBehavior::Exit   // 0 — zastaví přehrávání
PixEndBehavior::Repeat // 1 — opakuje od začátku (výchozí)
PixEndBehavior::Keep   // 2 — drží poslední snímek
```
Hodnota se čte ze souboru; nelze přepsat z kódu.

**API:**
```cpp
PixPlayer player(leds);
player.load("/test.pix");              // 0 = OK, jinak chybový kód
player.update();                       // volat z loop(); false = konec
player.startTask(core = 1, stackSize = 4096); // FreeRTOS task
player.stopTask();
player.unload();
player.isLoaded();                     // bool
player.numCommands();                  // int — počet příkazů v souboru
```

**Chybové kódy `load()`:**

| Kód | Význam |
|---|---|
| 2 | Chyba parsování / žádné příkazy |
| 3 | LittleFS se nepodařilo připojit |
| 4 | Chyba otevření/čtení souboru |
| 5 | Soubor nenalezen |
| 6 | Nedostatek paměti |

---

## Tok dat

```
LittleFS (.pix)
    │
    ▼
PixPlayer::load()
    ├── parseHeader()     → PixPlayer::_cmds[]
    └── preload / stream  → PSRAM buffer nebo File handle
    │
    ▼
PixPlayer::update()  [každých 1/frequency sekund]
    └── fetchColumn()     → ptr na [0xE0,B,G,R] × numLeds
    │
    ▼
APA102::showColumnDirect()
    └── memcpy → DMA tx buffer
    └── spi_device_queue_trans()  → SPI hardware → LED pásek
```

---

## Build

```bash
pio run                        # kompilace
pio run --target upload        # nahrání firmware
pio run --target uploadfs      # nahrání LittleFS (soubory z data/)
```

`.pix` soubory patří do složky `data/` v kořeni projektu. Pojmenovat `/test.pix` nebo upravit cestu v `main.cpp`.

### Partition tabulka (`partitions_8MB.csv`)

| Partition | Velikost |
|---|---|
| nvs | 20 KB |
| otadata | 8 KB |
| app0 (OTA 0) | 2 MB |
| app1 (OTA 1) | 2 MB |
| spiffs/littlefs | 3,875 MB |
| coredump | 64 KB |
