# AuraX — architektura firmware

## Přehled

Firmware pro ESP32-S3 (Seeed Studio XIAO ESP32S3) přehrávající animace ve formátu `.pix` na LED pásku. Podporuje **APA102** (SPI+DMA) a **WS281x / WS2812B** (RMT). Volba driveru: `LED_TYPE` v `src/config.h`.

---

## Hardware

| Parametr | Hodnota |
|---|---|
| MCU | ESP32-S3 |
| Board | Seeed Studio XIAO ESP32S3 |
| Flash | 8 MB (QIO, 80 MHz) |
| PSRAM | 8 MB OPI |
| LED protokol | APA102 (SPI) nebo WS281x (RMT) — volba v `src/config.h` |
| APA102 piny (testovací deska) | DATA = GPIO6, CLK = GPIO5 |
| APA102 piny (XIAO header) | DATA = GPIO9 (D10), CLK = GPIO7 (D8) |
| WS281x pin | DATA = GPIO6 (výchozí, konfig. `WS_DATA_PIN`) |

Piny GPIO6/5 procházejí GPIO matrix (ne nativní SPI linka), rozdíl výkonu je zanedbatelný.

---

## Moduly

### `led_driver.h` — abstraktní interface

`ILedDriver` je společný interface pro všechny LED drivery:

```cpp
class ILedDriver {
public:
    virtual uint16_t numLeds() const = 0;
    virtual void showColumnDirect(const uint8_t* pixData, uint16_t count) = 0;
    virtual void clear() = 0;
    virtual void setBrightness(uint8_t pct) = 0;  // 0–100 %, 100 = plný jas
};
```

`PixPlayer` pracuje s `ILedDriver&` — nezávisí na konkrétním driveru. Aktuální implementace: `APA102`, `WS281x`.

---

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
bool ok = leds.begin(freqHz = 20000000); // false = SPI init selhal
leds.setPixel(i, r, g, b, bri = 31);  // brightness 0–31
leds.fill(r, g, b, bri = 31);
leds.clear();
leds.show();                           // blocking
leds.showAsync();                      // non-blocking, DMA
leds.waitForShow();                    // čeká na dokončení DMA
leds.showColumnDirect(ptr, count);     // přímý DMA z pix bufferu
leds.setBrightness(pct);              // globální jas 0–100 %
leds.numLeds();                        // počet LED
```

---

### `ws281x.h / ws281x.cpp` — driver WS281x (WS2812B)

Řídí WS281x LED pásek přes ESP32 RMT peripheral.

**Klíčové vlastnosti:**
- ESP-IDF v4 RMT API (`driver/rmt.h`), kanál `RMT_CHANNEL_0`
- Clock: 40 MHz (clk_div=2), 25 ns/tick
- Double buffering — stejný async pattern jako APA102 (wait-at-start-of-next-call)
- Pixel encoding: `.pix` `[0xE0|bri, B, G, R]` → brightness scaling → GRB bity přes RMT

**WS2812B timing:**
```
Bit 1: 800 ns HIGH (32 ticks), 450 ns LOW (18 ticks)
Bit 0: 400 ns HIGH (16 ticks), 850 ns LOW (34 ticks)
Reset: 50 µs LOW (2000 ticks)
```

**Rychlostní strop:** 144 LED × 24 bit @ 800 kHz = ~4,3 ms/snímek → max **~230 řádků/s**. Nelze překonat bez změny protokolu.

**API:**
```cpp
WS281x leds(dataPin, numLeds, channel = RMT_CHANNEL_0);
bool ok = leds.begin();
leds.showColumnDirect(ptr, count);  // async, encodes brightness + GRB
leds.waitForShow();
leds.setBrightness(pct);            // globální jas 0–100 %
leds.numLeds();
```

---

### `pix_player.h / pix_player.cpp` — přehrávač .pix souborů

Parsuje `.pix` soubory z LittleFS a řídí LED pásek přes `ILedDriver`.

**Módy načítání:**
1. **Preload (PSRAM)** — celý obrazový obsah se načte do PSRAM (`MALLOC_CAP_SPIRAM`). Při 144 LED × 1000 sloupců ≈ 576 KB; 8MB PSRAM to bez problémů pojme. Čtení sloupce = pointer aritmetika, žádné I/O.
2. **Streaming (fallback)** — soubor zůstane otevřený, každý sloupec se načte seekem + `File.read()`. Použije se pokud PSRAM nestačí.

**Časování:** `esp_timer_get_time()` (mikrosekundová přesnost). Interval snímku = `1 000 000 / frequency` µs.

**Multi-command:** program může obsahovat více `picture_command` záznamů; přehrávač je postupně prochází. Po posledním se chování řídí `progEndBehavior` (Repeat / Keep / Exit).

**Pauzy:** mezera mezi `endTime` jednoho příkazu a `startTime` dalšího = pauza. Přehrávač při vstupu do pauzy zavolá `leds.clear()` (LEDky zhasnou) a čeká do začátku dalšího příkazu. Clear se volá jen jednou — flag `_inPause` zabraňuje opakování na každém ticku.

**Provozní mód — FreeRTOS task (doporučeno):**
Přehrávač běží jako dedikovaný task na core 1 s prioritou 5. `loop()` parkuje na `vTaskDelay(portMAX_DELAY)`. Core 0 zůstává volný pro WiFi a jiné úlohy.

Strategie čekání v `runTask()`:
- `> 10 ms` do dalšího snímku → `vTaskDelay` (uvolní CPU)
- `< 10 ms` → spinování s `taskYIELD()`, přesnost řídí `esp_timer_get_time()` (µs)

Tím je u APA102 dosažitelná frekvence přehrávání **1000+ řádků/s** bez závislosti na FreeRTOS tick rate. U WS281x platí jiný protokolový limit (~230 řádků/s).

**Přímá cesta (hot path) — APA102:**
Pixel v `.pix` souboru má formát `[0xE0|bri, B, G, R]` = přesně APA102 drátový formát → `showColumnDirect()` dělá jen `memcpy` do DMA bufferu, žádná konverze.

**Přímá cesta (hot path) — WS281x:**
`showColumnDirect()` volá `encodePixels()`: aplikuje brightness scaling (`R = R*bri/31`) a konvertuje na 24 RMT položek na LED v pořadí GRB, MSB first.

**`PixEndBehavior` enum:**
```cpp
PixEndBehavior::Exit   // 0 — zastaví přehrávání
PixEndBehavior::Repeat // 1 — opakuje od začátku (výchozí)
PixEndBehavior::Keep   // 2 — drží poslední snímek
```
Hodnota se čte ze souboru; runtime override přes `setEndBehavior(uint8_t v)` (0/1/2 nebo 255 = ze souboru, výchozí).

**API:**
```cpp
PixPlayer player(leds);               // leds: ILedDriver& (APA102 nebo WS281x)
player.load("/show.pix");             // 0 = OK, jinak chybový kód
player.update();                       // volat z loop(); false = konec
player.startTask(core = 1, stackSize = 4096); // FreeRTOS task
player.stopTask();                     // blokující — čeká na konec tasku
player.unload();
player.blackout();                     // stopTask + unload + leds.clear()
player.scheduleStart(int64_t atUs);    // naplánovat start na abs. čas; resetuje stats
player.setBrightness(pct);            // 0–100 %, deleguje na ILedDriver
player.setTempo(pct);                 // 100=normální, 50=poloviční, 200=dvojnásobná rychlost
player.setEndBehavior(v);             // 0=Exit, 1=Repeat, 2=Keep, 255=ze souboru (výchozí)
player.isLoaded();                    // bool
player.numCommands();                 // int — počet příkazů v souboru
player.stats();                       // Stats{framesRendered, framesExpected}
```

**`PixPlayer::Stats`** — statistiky přehrávání od posledního `load()` / `scheduleStart()`:

```cpp
struct Stats {
    uint32_t framesRendered;  // snímků skutečně vykreslených
    uint32_t framesExpected;  // snímků, které měly být vykresleny podle pozice v animaci
};
```

`framesExpected` = `loopCount × totalFrames + součet height dokončených příkazů + curCol`. Odvozuje se z pozice v animaci, nikoli z časovače — nezávisí na FreeRTOS jitteru. Vystaveno v `/status` jako `frames_rendered` / `frames_expected`; web UI zobrazuje varování `⚠` pokud `expected > rendered`.

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
ILedDriver::showColumnDirect()
    ├── APA102: memcpy → DMA tx buffer → spi_device_queue_trans() → SPI → LED
    └── WS281x: encodePixels() → RMT items → rmt_write_items() → RMT → LED
```

Binární formát `.pix` souborů: viz [pix-format.md](pix-format.md).

---

## Build

```bash
pio run                        # kompilace
pio run --target upload        # nahrání firmware
pio run --target uploadfs      # nahrání LittleFS (soubory z data/)
```

`.pix` soubory patří do složky `data/` v kořeni projektu. Výchozí cesta je `PIX_FILE = "/show.pix"` definovaná v `src/config.h`.

### Partition tabulka (`partitions_8MB.csv`)

| Partition | Velikost |
|---|---|
| nvs | 20 KB |
| otadata | 8 KB |
| app0 (OTA 0) | 2 MB |
| app1 (OTA 1) | 2 MB |
| spiffs/littlefs | 3,875 MB |
| coredump | 64 KB |

---

### `sync_control.h / sync_control.cpp` — ESP-NOW broadcast synchronizace

Zajišťuje synchronizované přehrávání, zastavení a efekty na více zařízeních současně přes ESP-NOW broadcast.

**Inicializace:** `begin()` musí být voláno **po** `wifi.begin()` — ESP-NOW potřebuje inicializovaný WiFi stack a platný kanál.

**Příkazy (ESP-NOW pakety):**

| CMD | Hodnota | Data | Popis |
|---|---|---|---|
| `CMD_PLAY` | 1 | `delayMs`, `endBehavior`, `file[64]` | Spustit animaci synchronizovaně |
| `CMD_STOP` | 2 | — | Zastavit animaci i efekt |
| `CMD_EFFECT` | 3 | `effectId`, `speed`, `dotSize`, `paletteSize`, `paletteR/G/B[4]` | Spustit efekt synchronizovaně |

**Synchronizace času:** `CMD_PLAY` nese `delayMs` (výchozí 200 ms). Odesílatel i přijímač spustí přehrávání v čase `now + delayMs`. 200 ms je dostatečná rezerva pro doručení ESP-NOW paketu a spuštění FreeRTOS tasku.

**Příjem paketů:** `recvCb()` je volán z ESP-NOW callback (ISR kontext) → nesmí blokovat, volat LittleFS ani `vTaskDelay`. Paket se uloží do FreeRTOS fronty (`xQueueSendFromISR`). `process()` se volá z `wifi_ctrl` tasku a zpracuje frontu bezpečně.

**API:**
```cpp
SyncControl sync(player, effectPlayer);
sync.begin();                                         // po wifi.begin()
sync.process();                                       // volat z wifi_ctrl task loop
sync.broadcastPlay("/show.pix", endBehavior, 200);   // sync play všem + lokálně
sync.broadcastStop();                                 // sync stop všem + lokálně
sync.broadcastEffect(params);                         // sync efekt všem + lokálně
```

**`broadcastStop()` a `/stop` / `/off`** zastavují jak `PixPlayer`, tak `EffectPlayer` — není třeba volat stop zvlášť pro každý.

**Packet struktura:**
```cpp
struct Packet {          // __attribute__((packed))
    uint8_t cmd;
    union {
        struct { uint32_t delayMs; uint8_t endBehavior; char file[64]; } play;
        struct { uint8_t effectId; uint16_t speed; uint8_t dotSize;
                 uint8_t paletteSize; uint8_t paletteR[4], paletteG[4], paletteB[4]; } effect;
    };
};                       // celkem 70 bytů (ESP-NOW limit: 250 B)
```
