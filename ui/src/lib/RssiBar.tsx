import { rssiStrength } from './utils'

interface Props {
  dbm?: number
  separator?: boolean
}

export function RssiBar({ dbm, separator = false }: Props) {
  if (!dbm) return null
  const bars = rssiStrength(dbm)
  return (
    <span>
      {separator && <>&nbsp;|&nbsp;</>}
      {'📶 ' + '█'.repeat(bars) + '░'.repeat(5 - bars) + ` (${dbm} dBm)`}
    </span>
  )
}
