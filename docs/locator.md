# AuraX web locator

Android hotspot nepreklada `aurax.local` spolehlive pro zarizeni pripojene k vlastnimu hotspotu telefonu. AuraX proto umi po pripojeni k WiFi poslat svoji lokalni IP na maly webovy locator.

Uzivatel potom otevre:

```text
https://go.aurax.cz
```

Locator najde posledni AuraX zarizeni, ktere se registrovalo ze stejne verejne IP adresy telefonu, a presmeruje prohlizec na aktualni lokalni IP, napr. `http://10.91.105.204/`.

Zalozni adresa pro konkretni zarizeni je:

```text
https://go.aurax.cz/<device-id>
```

## Deploy

1. V Cloudflare vytvor KV namespace `AURAX_LOCATOR`.
2. Zkopiruj `tools/locator-worker/wrangler.toml.example` na `wrangler.toml` a dopln KV namespace id.
3. Deploy:

```bash
cd tools/locator-worker
wrangler deploy
```

4. Pripoj domenu `go.aurax.cz` na worker.
5. Ve firmware musi `src/config.h` ukazovat na stejnou domenu:

```cpp
#define AURAX_LOCATOR_BASE_URL "https://go.aurax.cz"
#define AURAX_LOCATOR_ENDPOINT "https://go.aurax.cz/api/register"
```
