import type { Color } from './types'

export function colorToHex(c: Color): string {
  return (
    '#' +
    c.r.toString(16).padStart(2, '0') +
    c.g.toString(16).padStart(2, '0') +
    c.b.toString(16).padStart(2, '0')
  )
}

export function hexToColor(hex: string): Color {
  const h = hex.replace('#', '')
  return {
    r: parseInt(h.slice(0, 2), 16),
    g: parseInt(h.slice(2, 4), 16),
    b: parseInt(h.slice(4, 6), 16),
  }
}

export function rssiStrength(dbm: number): number {
  return dbm > -55 ? 5 : dbm > -65 ? 4 : dbm > -72 ? 3 : dbm > -80 ? 2 : 1
}

export function postJson(url: string, body: unknown): Promise<string> {
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }).then((r) => r.text())
}
