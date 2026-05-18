import { useState, useEffect } from 'preact/hooks'

interface Props {
  brightness?: number
  syncChannel?: number
}

export function BriSync({ brightness = 0, syncChannel = 0 }: Props) {
  const [bri, setBriState] = useState(brightness)
  const [ch, setCh] = useState(syncChannel)

  useEffect(() => setBriState(brightness), [brightness])
  useEffect(() => setCh(syncChannel), [syncChannel])

  function setBri(v: number) {
    setBriState(v)
    fetch(`/brightness?v=${v}`)
  }

  function setSyncCh(v: number) {
    setCh(v)
    fetch('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ syncChannel: v }),
    })
  }

  return (
    <div class="card" style="margin-top:14px">
      <div class="bri-row">
        <span class="label">☀ Jas</span>
        <input
          type="range" min={0} max={100} value={bri}
          onInput={(e) => setBri(+(e.currentTarget as HTMLInputElement).value)}
        />
        <span class="bri-val">{bri === 0 ? 'PIX soubor' : `${bri}%`}</span>
      </div>
      <div class="sync-row">
        <span class="label muted">Sync</span>
        <button onClick={() => fetch('/nudge?v=-100')}>◀ −100ms</button>
        <button onClick={() => fetch('/nudge?v=100')}>+100ms ▶</button>
        <select value={ch} onChange={(e) => setSyncCh(+(e.currentTarget as HTMLSelectElement).value)} class="ch-sel">
          <option value={0}>kanál: vyp</option>
          {Array.from({ length: 10 }, (_, i) => i + 1).map((n) => (
            <option key={n} value={n}>kanál: {n}</option>
          ))}
        </select>
      </div>
    </div>
  )
}
