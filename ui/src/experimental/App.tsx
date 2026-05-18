import { useState, useEffect } from 'preact/hooks'
import type { StatusResponse, PeerInfo, Config } from '../lib/types'
import type { EffectValues } from '../lib/EffectPanel'
import { StatusCard } from '../lib/StatusCard'
import { PeerList } from '../lib/PeerList'
import { BriSync } from '../lib/BriSync'
import { EffectPanel } from '../lib/EffectPanel'
import { UploadCard } from '../lib/UploadCard'
import { FirmwareCard } from '../lib/FirmwareCard'
import { ConfigForm } from './ConfigForm'

export function App() {
  const [status, setStatus] = useState<StatusResponse | null>(null)
  const [peers, setPeers] = useState<PeerInfo[]>([])
  const [config, setConfig] = useState<Config | null>(null)
  const [effect, setEffect] = useState<EffectValues>({
    effectId: 1, effectSpeed: 100, effectDotSize: 3,
    palette: [{ r: 255, g: 0, b: 0 }],
  })

  async function refresh() {
    const [s, p] = await Promise.all([
      fetch('/status').then((r) => r.json()),
      fetch('/peers').then((r) => r.json()).catch(() => []),
    ])
    setStatus(s)
    setPeers(p)
  }

  useEffect(() => {
    fetch('/config')
      .then((r) => r.json())
      .then((d: Config) => {
        setConfig(d)
        setEffect({
          effectId: d.effectId ?? 1,
          effectSpeed: d.effectSpeed ?? 100,
          effectDotSize: d.effectDotSize ?? 3,
          palette: d.paletteR
            ? d.paletteR.map((r, i) => ({ r, g: (d.paletteG ?? [])[i] ?? 0, b: (d.paletteB ?? [])[i] ?? 0 }))
            : [{ r: 255, g: 0, b: 0 }],
        })
      })
    refresh()
    const id = setInterval(refresh, 2000)
    return () => clearInterval(id)
  }, [])

  return (
    <>
      <p style="margin:0 0 16px">
        <a href="/" style="font-size:.85rem;color:var(--muted)">← Zpět</a>
      </p>
      <h1>AuraX <span class="exp-label">EXPERIMENTAL</span></h1>
      <div>
        <button class="play" onClick={() => fetch('/play').then(refresh)}>▶ Play</button>
        <button class="stop" onClick={() => fetch('/stop').then(refresh)}>■ Stop</button>
        <button class="stop" onClick={() => fetch('/off').then(refresh)}>⏻ Off</button>
      </div>
      <BriSync brightness={config?.brightness ?? 0} syncChannel={config?.syncChannel ?? 0} />
      <EffectPanel {...effect} onChange={(patch) => setEffect((prev) => ({ ...prev, ...patch }))} />
      <UploadCard onUploaded={refresh} />
      {status && <StatusCard status={status} />}
      <PeerList peers={peers} />
      <FirmwareCard />
      {config && <ConfigForm config={config} effect={effect} onEffectChange={(patch) => setEffect((prev) => ({ ...prev, ...patch }))} />}
    </>
  )
}
