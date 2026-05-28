import type { StatusResponse } from './types'
import { RssiBar } from './RssiBar'

export function StatusCard({ status }: { status: StatusResponse }) {
  const mode = status.wifi_mode === 1 ? 'Group master' : status.wifi_mode === 2 ? 'Group client' : ''
  return (
    <div class="card status">
      Stav: <b>{status.playing ? 'přehrává' : 'zastaveno'}</b>
      {status.file && <>&nbsp;|&nbsp;{status.file}</>}
      {!!status.commands && <>&nbsp;|&nbsp;příkazy: {status.commands}</>}
      {!!status.frames_expected && (
        <>
          &nbsp;|&nbsp;snímky: {status.frames_rendered}/{status.frames_expected}
          {(status.frames_expected ?? 0) > (status.frames_rendered ?? 0) && ' ⚠'}
        </>
      )}
      <br />
      {status.ap_mode ? '📶 AP: ' : 'IP: '}
      <a href={`http://${status.ip}`}>{status.ip}</a>
      {status.hostname && <>&nbsp;|&nbsp;{status.hostname}.local</>}
      {mode && <>&nbsp;|&nbsp;{mode}</>}
      {status.ap_mode && status.ap_ssid && <>&nbsp;|&nbsp;{status.ap_ssid}</>}
      {status.ap_mode && (
        <span style="color:var(--accent)">&nbsp;(Windows: použij IP odkaz)</span>
      )}
      &nbsp;|&nbsp;🔋 {status.battery_pct}% ({status.battery_mv} mV)
      {!status.ap_mode && <RssiBar dbm={status.rssi} separator />}
    </div>
  )
}
