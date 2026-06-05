import type { Color } from './types'
import { colorToHex, hexToColor } from './utils'

export interface EffectValues {
  effectId: number
  effectSpeed: number
  effectIntensity: number
  effectDotSize: number
  palette: Color[]
}

interface Props extends EffectValues {
  onChange: (patch: Partial<EffectValues>) => void
}

const EFFECTS = [
  [1, 'Solid'],
  [2, 'Android'],
  [10, 'BPM'],
  [11, 'Flow'],
  [12, 'Gravcenter'],
  [13, 'Gravfreq'],
  [52, 'Chase'],
  [14, 'Chase 2'],
  [15, 'Chase 3'],
  [16, 'Chunchun'],
  [17, 'Lake'],
  [18, 'Meteor'],
  [19, 'Noise 3'],
  [20, 'Oscillate'],
  [21, 'Ripple'],
  [22, 'Running'],
] as const

export function EffectPanel({ effectId, effectSpeed, effectIntensity, effectDotSize, palette, onChange }: Props) {
  function updateColor(i: number, hex: string) {
    const updated = [...palette]
    updated[i] = hexToColor(hex)
    onChange({ palette: updated })
  }

  function startEffect() {
    fetch('/effect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: effectId,
        speed: effectSpeed,
        intensity: effectIntensity,
        dotSize: effectDotSize,
        palette,
      }),
    })
  }

  return (
    <details class="card">
      <summary>Effects</summary>
      <div class="cfg-grid">
        <label>Effect
          <select value={effectId} onChange={(e) => onChange({ effectId: +(e.currentTarget as HTMLSelectElement).value })}>
            {EFFECTS.map(([id, name]) => (
              <option key={id} value={id}>{name}</option>
            ))}
          </select>
        </label>
        <label>Size
          <input type="number" value={effectDotSize} min={1} style="width:70px"
            onInput={(e) => onChange({ effectDotSize: +(e.currentTarget as HTMLInputElement).value })} />
        </label>
        <label style="grid-column:1/-1">Speed
          <input type="range" value={effectSpeed} min={0} max={255}
            onInput={(e) => onChange({ effectSpeed: +(e.currentTarget as HTMLInputElement).value })} />
        </label>
        <label style="grid-column:1/-1">Intensity
          <input type="range" value={effectIntensity} min={0} max={255}
            onInput={(e) => onChange({ effectIntensity: +(e.currentTarget as HTMLInputElement).value })} />
        </label>
      </div>
      <div class="palette-row">
        <span class="muted">Palette</span>
        {palette.map((color, i) => (
          <input key={i} type="color" value={colorToHex(color)} class="color-pick"
            onInput={(e) => updateColor(i, (e.currentTarget as HTMLInputElement).value)} />
        ))}
        {palette.length < 4 && (
          <button class="add-btn" onClick={() => onChange({ palette: [...palette, { r: 255, g: 0, b: 0 }] })}>+</button>
        )}
      </div>
      <button class="play" onClick={startEffect}>Start effect</button>
    </details>
  )
}
