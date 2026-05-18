import type { PeerInfo } from './types'
import { RssiBar } from './RssiBar'

export function PeerList({ peers }: { peers: PeerInfo[] }) {
  if (!peers.length) return null
  return (
    <div class="card">
      <div class="peers-heading">🔗 Zařízení v síti</div>
      {peers.map((p) => (
        <div class="peer-row" key={p.hostname}>
          <span class="peer-name">
            <a href={`http://${p.ip}`}>{p.hostname}.local</a>
            <span class="peer-ip">{p.ip}</span>
          </span>
          <span class="peer-meta">🔋 {p.bat_pct}%</span>
          {p.rssi && <span class="peer-meta"><RssiBar dbm={p.rssi} /></span>}
          {p.sync_channel && <span class="peer-meta peer-ch">ch&nbsp;{p.sync_channel}</span>}
        </div>
      ))}
    </div>
  )
}
