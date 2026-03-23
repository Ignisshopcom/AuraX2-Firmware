Spusť `pio device monitor` a vysvětli výstup.

Hledej tyto řádky podle Verification tabulky v CLAUDE.md:
- `[pix] preloaded ... bytes into PSRAM` → OK, PSRAM funguje
- `[pix] streaming mode` → PSRAM nedostupná, zkontroluj `memory_type = qio_opi`
- `[apa102] init failed` → SPI problém, zkontroluj GPIO piny v `src/config.h`
- `[pix] load failed: 5` → soubor chybí, spusť `pio run -t uploadfs`
- žádný výstup → připojit přes USB-C (USB-CDC), ne UART piny
