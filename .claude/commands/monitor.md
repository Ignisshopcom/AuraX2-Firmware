Spusť `pio device monitor` a vysvětli výstup.

Hledej tyto řádky podle Verification tabulky v CLAUDE.md:
- `[pix] preloaded ... bytes into PSRAM` → OK, PSRAM funguje
- `[pix] streaming mode` → PSRAM nedostupná, zkontroluj `memory_type = qio_opi`
- `[apa102] init failed` → SPI problém, zkontroluj GPIO piny v `src/config.h`
- `[pix] load failed: 5` → soubor chybí, spusť `pio run -t uploadfs`
- `[pix] load failed: 3` → LittleFS se nepodařilo připojit, zkontroluj partition tabulku
- `[ws281x] init failed` → RMT inicializace selhala, zkontroluj `WS_DATA_PIN` v `src/config.h`
- žádný výstup → nejprve zkontroluj `#define PIX_DEBUG` v `src/config.h` — bez toho se `Serial.begin()` nikdy nevolá; teprve pak řeš fyzické připojení (USB-C, ne UART piny)
