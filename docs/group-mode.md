# AuraX Group mode

Group mode je offline rezim pro ovladani vice AuraX zarizeni bez internetu,
bez domeny a bez mobilni aplikace.

## Rezimy

- `WiFi / hotspot klient`: AuraX se pripoji k bezne WiFi nebo hotspotu telefonu.
  Kdyz se nepripoji, spusti vlastni fallback AP `AuraX-XXXX`.
- `Group master`: AuraX vytvori spolecnou WiFi sit, vychozi `AuraX-GROUP`
  s heslem `aurax1234`. Telefon i ostatni AuraX zarizeni se pripoji do teto site.
- `Group client`: AuraX se pripoji do site vytvorene Group masterem.
  Pokud master neni dostupny, spusti fallback AP pro nastaveni.

## Doporuceny setup

1. Jedno zarizeni nastav jako `Group master`.
2. Ostatni zarizeni nastav jako `Group client`.
3. Nech stejne `Group SSID` a `Group heslo` na vsech zarizenich.
4. Telefon pripoj na WiFi `AuraX-GROUP`.
5. Otevri ovladani pres captive portal nebo `http://192.168.4.1`.

Group master zobrazi ostatni zarizeni v seznamu peeru pres UDP discovery. Sync
prikazy zustavaji filtrovane pres `syncChannel`, stejne jako v beznem rezimu.

## Android app

`android/AuraXFinder` je jednoducha Android appka pro vyhledani AuraX zarizeni
v aktualni siti. Po startu:

- posle UDP dotaz `AURAX?` na port `4210`,
- posloucha odpovedi `AURAX <hostname> <ip> ...`,
- zkusi HTTP scan lokalnich privatnich siti pres `/status`,
- otevre web UI zarizeni v aplikaci pres WebView.

Appka nepouziva cloud ani internet. Slouzi jen jako finder a wrapper nad web UI,
ktere porad bezi primo v ESP.

## Poznamky

- Group master ma zapnute AP, takze spotrebuje vic energie nez client.
- Pro samostatnou ovladaci krabicku lze pouzit stejny firmware a nastavit ji
  jako `Group master`.
- `aurax.local` zustava best-effort pro routery a PC, ale Group mode se na nej
  nespoleha.
