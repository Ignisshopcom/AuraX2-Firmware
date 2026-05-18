#!/usr/bin/env python3
"""OTA flash firmware to all AuraX devices.

Usage:
  python3 tools/flash_all.py aurax-hard.local
  python3 tools/flash_all.py 192.168.1.42
"""
import sys
import requests

FIRMWARE = ".pio/build/esp32s3dev_8MB_PSRAM_opi/firmware.bin"
UPLOAD_TIMEOUT = 90


def flash(ip: str, fw: bytes) -> bool:
    print(f"  → {ip} ... ", end="", flush=True)
    try:
        r = requests.post(
            f"http://{ip}/update",
            files={"firmware": ("firmware.bin", fw, "application/octet-stream")},
            timeout=UPLOAD_TIMEOUT,
        )
        print(r.text.strip())
        return r.status_code == 200
    except Exception as e:
        print(f"CHYBA: {e}")
        return False


def main():
    if len(sys.argv) < 2:
        print("Usage: flash_all.py <hostname|ip>")
        sys.exit(1)

    seed = sys.argv[1]
    print(f"Hledám zařízení přes {seed}...")

    try:
        status = requests.get(f"http://{seed}/status", timeout=5).json()
        peers  = requests.get(f"http://{seed}/peers",  timeout=5).json()
    except Exception as e:
        print(f"Nelze spojit s {seed}: {e}")
        sys.exit(1)

    targets = [{"hostname": status.get("hostname", seed), "ip": status["ip"]}]
    for p in peers:
        targets.append({"hostname": p["hostname"], "ip": p["ip"]})

    for t in targets:
        print(f"  {t['hostname']}.local ({t['ip']})")

    print(f"\nNalezeno {len(targets)} zařízení.")
    try:
        if input("Flashovat všechny? [y/N] ").strip().lower() != "y":
            print("Zrušeno.")
            sys.exit(0)
    except EOFError:
        sys.exit(0)

    try:
        fw = open(FIRMWARE, "rb").read()
        print(f"\nFirmware: {len(fw) / 1024:.1f} kB\n")
    except FileNotFoundError:
        print(f"Firmware nenalezen: {FIRMWARE}\nSpusť nejdřív: pio run")
        sys.exit(1)

    failed = [t["hostname"] for t in targets if not (print(f"[{t['hostname']}.local]") or flash(t["ip"], fw))]

    print()
    if failed:
        print(f"❌ Selhalo {len(failed)}/{len(targets)}: {', '.join(failed)}")
        sys.exit(1)
    else:
        print(f"✅ Všechna zařízení ({len(targets)}) úspěšně flashována.")


if __name__ == "__main__":
    main()
