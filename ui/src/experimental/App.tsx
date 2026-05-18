import { useState, useEffect } from 'preact/hooks'
import type { StatusResponse, PeerInfo, Config } from '../lib/types'
import { StatusCard } from '../lib/StatusCard'
import { PeerList } from '../lib/PeerList'
import { BriSync } from '../lib/BriSync'
import { UploadCard } from '../lib/UploadCard'
import { ConfigForm } from './ConfigForm'

export function App() {
  const [status, setStatus] = useState<StatusResponse | null>(null)
  const [peers, setPeers] = useState<PeerInfo[]>([])
  const [config, setConfig] = useState<Config | null>(null)

  async function refresh() {
    const [s, p] = await Promise.all([
      fetch('/status').then((r) => r.json()),
      fetch('/peers').then((r) => r.json()).catch(() => []),
    ])
    setStatus(s)
    setPeers(p)
  }

  useEffect(() => {
    fetch('/config').then((r) => r.json()).then(setConfig)
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
      <UploadCard onUploaded={refresh} />
      {status && <StatusCard status={status} />}
      <PeerList peers={peers} />
      {config && <ConfigForm config={config} />}
    </>
  )
}
