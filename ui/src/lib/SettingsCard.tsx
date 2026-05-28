import { useState } from 'preact/hooks'
import type { Config } from './types'

export function SettingsCard({ config }: { config: Config }) {
  const [hostname, setHostname] = useState(config.hostname ?? 'aurax')
  const [wifiMode, setWifiMode] = useState(config.wifiMode ?? 0)
  const [ssid, setSsid] = useState(config.ssid ?? '')
  const [password, setPassword] = useState(config.password ?? '')
  const [groupSsid, setGroupSsid] = useState(config.groupSsid ?? 'AuraX-GROUP')
  const [groupPassword, setGroupPassword] = useState(config.groupPassword ?? 'aurax1234')
  const [msg, setMsg] = useState('')

  function save() {
    fetch('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hostname, wifiMode, ssid, password, groupSsid, groupPassword }),
    }).then((r) => r.text()).then(setMsg)
  }

  function reboot() {
    fetch('/reboot', { method: 'POST' })
    setMsg('Rebootuji...')
  }

  function updateFw(e: Event) {
    const file = (e.currentTarget as HTMLInputElement).files?.[0]
    if (!file) return
    setMsg('Nahrávám firmware...')
    const fd = new FormData()
    fd.append('firmware', file, file.name)
    fetch('/update', { method: 'POST', body: fd })
      .then((r) => r.text())
      .then(setMsg)
      .catch(() => setMsg('Chyba nahrávání'))
  }

  return (
    <details class="card">
      <summary>⚙ Nastavení</summary>
      <div class="cfg-grid" style="margin-top:12px">
        <label style="grid-column:1/-1">Režim sítě
          <select value={wifiMode} onChange={(e) => setWifiMode(+(e.currentTarget as HTMLSelectElement).value)}>
            <option value={0}>WiFi / hotspot klient</option>
            <option value={1}>Group master</option>
            <option value={2}>Group client</option>
          </select>
        </label>
        <label style="grid-column:1/-1">Hostname (.local)
          <input type="text" value={hostname} pattern="[a-z0-9-]+" placeholder="aurax"
            onInput={(e) => setHostname((e.currentTarget as HTMLInputElement).value)} style="width:100%" />
        </label>
        {wifiMode === 0 ? (
          <>
            <label>WiFi SSID
              <input type="text" value={ssid} onInput={(e) => setSsid((e.currentTarget as HTMLInputElement).value)} />
            </label>
            <label>WiFi heslo
              <input type="password" value={password} onInput={(e) => setPassword((e.currentTarget as HTMLInputElement).value)} />
            </label>
          </>
        ) : (
          <>
            <label>Group SSID
              <input type="text" value={groupSsid} onInput={(e) => setGroupSsid((e.currentTarget as HTMLInputElement).value)} />
            </label>
            <label>Group heslo
              <input type="password" value={groupPassword} minLength={8} onInput={(e) => setGroupPassword((e.currentTarget as HTMLInputElement).value)} />
            </label>
          </>
        )}
      </div>
      <div style="margin-top:4px">
        <button type="button" class="save" onClick={save}>Uložit</button>
        <button type="button" class="reboot" onClick={reboot}>Reboot</button>
      </div>
      {msg && <p class="cfg-msg">{msg}</p>}
      <hr />
      <div class="fw-row">
        <span class="fw-label">Firmware (.bin)</span>
        <input type="file" accept=".bin" class="fw-input" onChange={updateFw} />
      </div>
    </details>
  )
}
