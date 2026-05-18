import type { Color } from './types'
import { colorToHex, hexToColor } from './utils'

export interface EffectValues {
  effectId: number
  effectSpeed: number
  effectDotSize: number
  palette: Color[]
}

interface Props extends EffectValues {
  onChange: (patch: Partial<EffectValues>) => void
}

export function EffectPanel({ effectId, effectSpeed, effectDotSize, palette, onChange }: Props) {
  function updateColor(i: number, hex: string) {
    const updated = [...palette]
    updated[i] = hexToColor(hex)
    onChange({ palette: updated })
  }

  function startEffect() {
    fetch('/effect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: effectId, speed: effectSpeed, dotSize: effectDotSize, palette }),
    })
  }

  return (
    <details class="card">
      <summary>✨ Efekty</summary>
      <div class="cfg-grid">
        <label>Efekt
          <select value={effectId} onChange={(e) => onChange({ effectId: +(e.currentTarget as HTMLSelectElement).value })}>
            <option value={1}>Solid</option>
            <option value={2}>Android</option>
          </select>
        </label>
        <label>Šířka
          <input type="number" value={effectDotSize} min={1} max={20} style="width:70px"
            onInput={(e) => onChange({ effectDotSize: +(e.currentTarget as HTMLInputElement).value })} />
        </label>
        <label style="grid-column:1/-1">Rychlost
          <input type="range" value={effectSpeed} min={10} max={1000}
            onInput={(e) => onChange({ effectSpeed: +(e.currentTarget as HTMLInputElement).value })} />
        </label>
      </div>
      <div class="palette-row">
        <span class="muted">Paleta</span>
        {palette.map((color, i) => (
          <input key={i} type="color" value={colorToHex(color)} class="color-pick"
            onInput={(e) => updateColor(i, (e.currentTarget as HTMLInputElement).value)} />
        ))}
        {palette.length < 4 && (
          <button class="add-btn" onClick={() => onChange({ palette: [...palette, { r: 255, g: 0, b: 0 }] })}>+</button>
        )}
      </div>
      <button class="play" onClick={startEffect}>▶ Spustit efekt</button>
    </details>
  )
}
