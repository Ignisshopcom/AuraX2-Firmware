import { useState } from 'preact/hooks'
import type { Config } from './types'

export function SettingsCard({ config }: { config: Config }) {
  const [hostname, setHostname] = useState(config.deviceName ?? config.hostname ?? 'aurax')
  const [ssid, setSsid] = useState(config.ssid ?? '')
  const [password, setPassword] = useState(config.password ?? '')
  const [msg, setMsg] = useState('')

  function save() {
    fetch('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hostname, ssid, password }),
    }).then((r) => r.text()).then(setMsg)
  }

  function reboot() {
    fetch('/reboot', { method: 'POST' })
    setMsg('Rebootuji...')
  }

  function updateFw(e: Event) {
    const file = (e.currentTarget as HTMLInputElement).files?.[0]
    if (!file) return
    setMsg('Nahravam firmware...')
    const fd = new FormData()
    fd.append('firmware', file, file.name)
    fetch('/update', { method: 'POST', body: fd })
      .then((r) => r.text())
      .then(setMsg)
      .catch(() => setMsg('Chyba nahravani'))
  }

  return (
    <details class="card">
      <summary>Nastaveni</summary>
      <div class="cfg-grid" style="margin-top:12px">
        <label style="grid-column:1/-1">Device name
          <input type="text" value={hostname} pattern="[a-z0-9-]+" placeholder="aurax"
            onInput={(e) => setHostname((e.currentTarget as HTMLInputElement).value)} style="width:100%" />
        </label>
        <label>WiFi SSID
          <input type="text" value={ssid} onInput={(e) => setSsid((e.currentTarget as HTMLInputElement).value)} />
        </label>
        <label>WiFi heslo
          <input type="password" value={password} onInput={(e) => setPassword((e.currentTarget as HTMLInputElement).value)} />
        </label>
      </div>
      <div style="margin-top:4px">
        <button type="button" class="save" onClick={save}>Ulozit</button>
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
