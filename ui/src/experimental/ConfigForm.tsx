import { useState } from 'preact/hooks'
import type { Config, Color } from '../lib/types'
import type { EffectValues } from '../lib/EffectPanel'
import { EffectPanel } from '../lib/EffectPanel'

type FormState = {
  ledType: number; numLeds: number; dataPin: number; clkPin: number
  pixFile: string; ssid: string; password: string; hostname: string
  syncChannel: number; mALimit: number; batPin: number; batMultiplier: number
  batCalibration: number; batMinMv: number; batMaxMv: number
  batIntervalMs: number; batAutoOff: boolean; batAutoOffThreshold: number
}

export function ConfigForm({ config }: { config: Config }) {
  const [msg, setMsg] = useState('')
  const [form, setForm] = useState<FormState>({
    ledType: config.ledType ?? 1, numLeds: config.numLeds ?? 144,
    dataPin: config.dataPin ?? 6, clkPin: config.clkPin ?? 5,
    pixFile: config.pixFile ?? '/show.pix', ssid: config.ssid ?? '',
    password: config.password ?? '', hostname: config.hostname ?? 'aurax',
    syncChannel: config.syncChannel ?? 0, mALimit: config.mALimit ?? 0,
    batPin: config.batPin ?? 2, batMultiplier: config.batMultiplier ?? 2.0,
    batCalibration: config.batCalibration ?? 0.0, batMinMv: config.batMinMv ?? 3200,
    batMaxMv: config.batMaxMv ?? 4200, batIntervalMs: config.batIntervalMs ?? 30000,
    batAutoOff: config.batAutoOff ?? false, batAutoOffThreshold: config.batAutoOffThreshold ?? 10,
  })
  const set = (patch: Partial<FormState>) => setForm((prev) => ({ ...prev, ...patch }))

  const [effect, setEffect] = useState<EffectValues>({
    effectId: config.effectId ?? 1, effectSpeed: config.effectSpeed ?? 100,
    effectDotSize: config.effectDotSize ?? 3,
    palette: (config.paletteR ?? [255]).map((r, i) => ({
      r, g: (config.paletteG ?? [])[i] ?? 0, b: (config.paletteB ?? [])[i] ?? 0,
    })) as Color[],
  })

  function save() {
    fetch('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ...form, ...effect,
        paletteSize: effect.palette.length,
        paletteR: effect.palette.map((c) => c.r),
        paletteG: effect.palette.map((c) => c.g),
        paletteB: effect.palette.map((c) => c.b),
      }),
    }).then((r) => r.text()).then(setMsg)
  }

  function reboot() {
    fetch('/reboot', { method: 'POST' })
    setMsg('Rebootuji...')
  }

  const num = (e: Event) => +(e.currentTarget as HTMLInputElement).value
  const str = (e: Event) => (e.currentTarget as HTMLInputElement).value

  return (
    <details class="card">
      <summary>⚙ Nastavení</summary>
      <div class="cfg-grid">
        <label>LED typ
          <select value={form.ledType} onChange={(e) => set({ ledType: num(e) })}>
            <option value={1}>APA102</option>
            <option value={0}>WS281x</option>
          </select>
        </label>
        <label>Počet LED
          <input type="number" value={form.numLeds} min={1} max={2048} onInput={(e) => set({ numLeds: num(e) })} />
        </label>
        <label>Data pin
          <input type="number" value={form.dataPin} min={0} max={48} onInput={(e) => set({ dataPin: num(e) })} />
        </label>
        {form.ledType === 1 && (
          <label>CLK pin
            <input type="number" value={form.clkPin} min={0} max={48} onInput={(e) => set({ clkPin: num(e) })} />
          </label>
        )}
        <label style="grid-column:1/-1">PIX soubor
          <input type="text" value={form.pixFile} onInput={(e) => set({ pixFile: str(e) })} style="width:100%" />
        </label>
        <label>WiFi SSID
          <input type="text" value={form.ssid} onInput={(e) => set({ ssid: str(e) })} />
        </label>
        <label>WiFi heslo
          <input type="password" value={form.password} onInput={(e) => set({ password: str(e) })} />
        </label>
        <label style="grid-column:1/-1">Hostname (.local)
          <input type="text" value={form.hostname} pattern="[a-z0-9-]+" placeholder="aurax-xxxx"
            onInput={(e) => set({ hostname: str(e) })} style="width:100%" />
        </label>
        <label>Sync kanál
          <select value={form.syncChannel} onChange={(e) => set({ syncChannel: num(e) })}>
            <option value={0}>Vypnuto</option>
            {Array.from({ length: 10 }, (_, i) => i + 1).map((n) => (
              <option key={n} value={n}>{n}</option>
            ))}
          </select>
        </label>
        <label>Limit mA (0=off)
          <input type="number" value={form.mALimit} min={0} max={65000} step={100} onInput={(e) => set({ mALimit: num(e) })} />
        </label>
      </div>

      <details class="bat-details">
        <summary>🔋 Baterie</summary>
        <div class="cfg-grid" style="margin-top:10px">
          <label>ADC pin <input type="number" value={form.batPin} min={0} max={48} onInput={(e) => set({ batPin: num(e) })} /></label>
          <label>Multiplier <input type="number" value={form.batMultiplier} min={0.1} max={20} step={0.001} onInput={(e) => set({ batMultiplier: num(e) })} /></label>
          <label>Kalibrace (V) <input type="number" value={form.batCalibration} step={0.001} onInput={(e) => set({ batCalibration: num(e) })} /></label>
          <label>Min mV <input type="number" value={form.batMinMv} min={0} max={5000} onInput={(e) => set({ batMinMv: num(e) })} /></label>
          <label>Max mV <input type="number" value={form.batMaxMv} min={0} max={5000} onInput={(e) => set({ batMaxMv: num(e) })} /></label>
          <label style="grid-column:1/-1">Interval měření (ms)
            <input type="number" value={form.batIntervalMs} min={100} max={3600000} step={1000} style="width:130px"
              onInput={(e) => set({ batIntervalMs: num(e) })} />
          </label>
          <label style="grid-column:1/-1;flex-direction:row;align-items:center;gap:8px;color:var(--text)">
            <input type="checkbox" checked={form.batAutoOff} onChange={(e) => set({ batAutoOff: (e.currentTarget as HTMLInputElement).checked })} />
            Auto off pod
            <input type="number" value={form.batAutoOffThreshold} min={0} max={100} style="width:60px"
              onInput={(e) => set({ batAutoOffThreshold: num(e) })} />%
          </label>
        </div>
      </details>

      <EffectPanel {...effect} onChange={(patch) => setEffect((prev) => ({ ...prev, ...patch }))} />

      <div style="margin-top:4px">
        <button type="button" class="save" onClick={save}>Uložit</button>
        <button type="button" class="reboot" onClick={reboot}>Reboot</button>
      </div>
      {msg && <p class="cfg-msg">{msg}</p>}

      <hr />
      <div class="fw-row">
        <span class="fw-label">Firmware (.bin)</span>
        <input type="file" accept=".bin" class="fw-input" onChange={(e) => {
          const file = (e.currentTarget as HTMLInputElement).files?.[0]
          if (!file) return
          setMsg('Nahrávám firmware...')
          const fd = new FormData()
          fd.append('firmware', file, file.name)
          fetch('/update', { method: 'POST', body: fd }).then((r) => r.text()).then(setMsg)
        }} />
      </div>
    </details>
  )
}
