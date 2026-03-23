Flashni firmware a LittleFS na připojenou desku.

1. Spusť `pio run` (kompilace) — zastav při chybě
2. Spusť `pio run -t uploadfs` (LittleFS) — zastav při chybě
3. Spusť `pio run -t upload` (firmware) — zastav při chybě
4. Oznam výsledek. Připomeň uživateli, aby zkontroloval sériový monitor (`pio device monitor`) a ověřil výstup podle sekce Verification v CLAUDE.md.
