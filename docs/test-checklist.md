# AuraX — testovací checklist

Pokrývá commity `003109f` a `7a97de5`.

---

## Boot a WLED import

- [ ] Flashnout FW na zařízení s existujícím WLED `/cfg.json` (bez `uploadfs`) → zařízení se připojí na správnou WiFi; v Nastavení sedí počet LED, typ, piny, hostname
- [ ] Totéž bez `/cfg.json` na FS → nastartuje normálně s compile-time defaults
- [ ] Po importu restartovat → import se neopakuje (sériový monitor nevypíše `imported settings from WLED`)

## Pamatování stavu po restartu

- [ ] Spustit efekt → vypnout napájení → zapnout → efekt se obnoví
- [ ] Spustit program (Play) → vypnout → zapnout → program se přehraje
- [ ] Crash recovery: watchdog/panic reset → po restartu se nic nespustí automaticky (`[sys] crash detected`)

## Efekty

- [ ] dotSize = 1 → efekt běží, nezasekne
- [ ] dotSize = numLeds (maximum) → efekt běží, nezasekne
- [ ] Rychlost slider na minimum (10) → efekt viditelně pomalý
- [ ] Rychlost slider na maximum (1000) → efekt viditelně rychlý
- [ ] Přidat 4 barvy do palety → všechny se zobrazí
- [ ] Pokusit se přidat pátou barvu → UI neumožní (tlačítko + nereaguje)
- [ ] Spustit efekt → Play → efekt se zastaví, spustí animace
- [ ] Spustit animaci → Spustit efekt → animace se zastaví, spustí efekt

## Animace a PingPong

- [ ] Nahrát mirror animaci → běží správnou rychlostí (ne 2×)
- [ ] Nudge ◀ −100 ms → animace se viditelně posune dozadu
- [ ] Nudge +100 ms ▶ → animace se viditelně posune dopředu

## Nastavení — validace

- [ ] `POST /config` s `{"numLeds":0}` → stará hodnota zůstane (HTTP 200, reboot → numLeds nezměněn)
- [ ] `POST /config` s `{"batMinMv":4000,"batMaxMv":3000}` → hodnoty se neuloží
- [ ] `POST /config` s `{"batIntervalMs":0}` → hodnota se neuloží
- [ ] Uložit validní nastavení → reboot → všechny hodnoty přežijí

## Baterie

- [ ] Zapsat do `/config.json` ručně `"batMinMv":3700,"batMaxMv":3700` → reboot → zařízení nesmí crashnout, `battery_pct` = 100 %

## ESP-NOW sync *(vyžaduje dvě zařízení)*

- [ ] Broadcast Play → obě zařízení startují synchronně
- [ ] Broadcast Stop → obě zastaví
- [ ] Broadcast Effect → obě zobrazí stejný efekt
- [ ] Malformed paket na stejném sync kanálu → přijímající zařízení se nezasekne
