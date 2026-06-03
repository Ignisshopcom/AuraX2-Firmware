import { useEffect, useRef, useState } from 'preact/hooks'
import type { Color, Config, PeerInfo, ProgramsResponse, StatusResponse } from './types'
import { colorToHex, colorToHsv, hsvToColor, postJson, rssiPercent } from './utils'

type Tab = 'programs' | 'colors' | 'effects' | 'settings'

type EffectState = {
  effectId: number
  speed: number
  intensity: number
  size: number
  paletteId: number
  colors: Color[]
}

type EffectMeta = {
  id: number
  name: string
  speed?: string
  intensity?: string
  size?: string
  sizeMax?: number
}

const EFFECTS: EffectMeta[] = [
  { id: 1, name: 'Solid' },
  { id: 2, name: 'Android', speed: 'Speed', size: 'Width', sizeMax: 32 },
  { id: 10, name: 'BPM', speed: 'BPM', intensity: 'Beat depth' },
  { id: 11, name: 'Flow', speed: 'Speed', intensity: 'Waves' },
  { id: 12, name: 'Gravcenter', speed: 'Speed', size: 'Width', sizeMax: 32 },
  { id: 13, name: 'Gravfreq', speed: 'Speed', intensity: 'Frequency' },
  { id: 14, name: 'Chase 2', speed: 'Speed', size: 'Tail', sizeMax: 32 },
  { id: 15, name: 'Chase 3', speed: 'Speed', size: 'Tail', sizeMax: 32 },
  { id: 16, name: 'Chunchun', speed: 'Speed', size: 'Width', sizeMax: 32 },
  { id: 17, name: 'Lake', speed: 'Speed', intensity: 'Wave depth' },
  { id: 18, name: 'Meteor', speed: 'Speed', intensity: 'Trail fade', size: 'Tail', sizeMax: 40 },
  { id: 19, name: 'Noise 3', speed: 'Speed', intensity: 'Scale' },
  { id: 20, name: 'Oscillate', speed: 'Speed', size: 'Width', sizeMax: 32 },
  { id: 21, name: 'Ripple', speed: 'Speed', size: 'Width', sizeMax: 32 },
  { id: 22, name: 'Running', speed: 'Speed', intensity: 'Density' },
  { id: 23, name: 'Strobe', speed: 'Rate', intensity: 'Duty' },
  { id: 24, name: 'Fade', speed: 'Speed', intensity: 'Depth' },
  { id: 25, name: 'Rainbow', speed: 'Speed', intensity: 'Spread' },
]

const PALETTES = [
  { id: 0, name: 'Custom', colors: [{ r: 255, g: 96, b: 0 }, { r: 0, g: 180, b: 255 }, { r: 255, g: 255, b: 255 }] },
  { id: 1, name: 'Rainbow', colors: [{ r: 255, g: 0, b: 0 }, { r: 0, g: 255, b: 0 }, { r: 0, g: 0, b: 255 }] },
  { id: 2, name: 'Fire', colors: [{ r: 0, g: 0, b: 0 }, { r: 255, g: 72, b: 0 }, { r: 255, g: 230, b: 120 }] },
  { id: 3, name: 'Ocean', colors: [{ r: 0, g: 12, b: 80 }, { r: 0, g: 170, b: 255 }, { r: 120, g: 255, b: 220 }] },
  { id: 4, name: 'Forest', colors: [{ r: 0, g: 30, b: 0 }, { r: 0, g: 140, b: 36 }, { r: 220, g: 255, b: 80 }] },
  { id: 5, name: 'Party', colors: [{ r: 255, g: 0, b: 80 }, { r: 0, g: 220, b: 255 }, { r: 255, g: 220, b: 0 }] },
  { id: 6, name: 'Sunset', colors: [{ r: 60, g: 0, b: 80 }, { r: 255, g: 72, b: 0 }, { r: 255, g: 190, b: 70 }] },
  { id: 7, name: 'Ice', colors: [{ r: 0, g: 40, b: 120 }, { r: 120, g: 230, b: 255 }, { r: 255, g: 255, b: 255 }] },
  { id: 8, name: 'Lava', colors: [{ r: 0, g: 0, b: 0 }, { r: 160, g: 0, b: 0 }, { r: 255, g: 220, b: 120 }] },
  { id: 9, name: 'Cloud', colors: [{ r: 15, g: 28, b: 70 }, { r: 90, g: 145, b: 220 }, { r: 240, g: 248, b: 255 }] },
  { id: 10, name: 'Pastel', colors: [{ r: 255, g: 132, b: 192 }, { r: 124, g: 255, b: 190 }, { r: 132, g: 190, b: 255 }] },
  { id: 11, name: 'Neon', colors: [{ r: 255, g: 0, b: 220 }, { r: 0, g: 255, b: 255 }, { r: 130, g: 255, b: 0 }] },
  { id: 12, name: 'Red White', colors: [{ r: 255, g: 0, b: 0 }, { r: 255, g: 255, b: 255 }, { r: 255, g: 0, b: 0 }] },
  { id: 13, name: 'Blue White', colors: [{ r: 0, g: 70, b: 255 }, { r: 255, g: 255, b: 255 }, { r: 0, g: 70, b: 255 }] },
  { id: 14, name: 'Magenta Orange', colors: [{ r: 255, g: 0, b: 180 }, { r: 255, g: 110, b: 0 }, { r: 255, g: 0, b: 180 }] },
  { id: 15, name: 'Green Blue', colors: [{ r: 0, g: 255, b: 70 }, { r: 0, g: 80, b: 255 }, { r: 0, g: 255, b: 70 }] },
  { id: 16, name: 'Candy', colors: [{ r: 255, g: 40, b: 110 }, { r: 255, g: 255, b: 255 }, { r: 80, g: 210, b: 255 }] },
  { id: 17, name: 'Aurora', colors: [{ r: 24, g: 16, b: 100 }, { r: 0, g: 220, b: 170 }, { r: 160, g: 80, b: 255 }] },
  { id: 18, name: 'Vintage', colors: [{ r: 80, g: 20, b: 10 }, { r: 220, g: 120, b: 36 }, { r: 20, g: 90, b: 95 }] },
]

const DEFAULT_EFFECT: EffectState = {
  effectId: 1,
  speed: 100,
  intensity: 128,
  size: 3,
  paletteId: 0,
  colors: PALETTES[0].colors,
}

function fmtBytes(n: number): string {
  if (n >= 1048576) return `${(n / 1048576).toFixed(2)} MB`
  if (n >= 1024) return `${Math.round(n / 1024)} kB`
  return `${n} B`
}

function fmtFps(status: StatusResponse | null): string {
  const fpsX10 = status?.fps_x10 ?? 0
  return `${(fpsX10 / 10).toFixed(1)} FPS`
}

function readEffect(config: Config | null): EffectState {
  if (!config) return DEFAULT_EFFECT
  const colors = config.paletteR
    ? config.paletteR.slice(0, 3).map((r, i) => ({
        r,
        g: (config.paletteG ?? [])[i] ?? 0,
        b: (config.paletteB ?? [])[i] ?? 0,
      }))
    : DEFAULT_EFFECT.colors
  while (colors.length < 3) colors.push({ r: 0, g: 0, b: 0 })
  return {
    effectId: config.effectId ?? 1,
    speed: config.effectSpeed ?? 100,
    intensity: config.effectIntensity ?? 128,
    size: config.effectDotSize ?? 3,
    paletteId: config.effectPaletteId ?? 0,
    colors,
  }
}

function colorSlotsForEffect(effectId: number): number {
  if (effectId === 25) return 0
  if (effectId === 1 || effectId === 23) return 1
  if (effectId === 2) return 2
  return 3
}

function effectPayload(effect: EffectState, persist = true) {
  const count = Math.max(1, colorSlotsForEffect(effect.effectId))
  return {
    id: effect.effectId,
    speed: effect.speed,
    intensity: effect.intensity,
    dotSize: effect.size,
    paletteId: effect.paletteId,
    colors: effect.colors.slice(0, count),
    persist,
  }
}

export function AuraXApp() {
  const [tab, setTab] = useState<Tab>('programs')
  const [status, setStatus] = useState<StatusResponse | null>(null)
  const [peers, setPeers] = useState<PeerInfo[]>([])
  const [config, setConfig] = useState<Config | null>(null)
  const [programs, setPrograms] = useState<ProgramsResponse | null>(null)
  const [selectedSlot, setSelectedSlot] = useState(1)
  const [effect, setEffect] = useState<EffectState>(DEFAULT_EFFECT)
  const [activeColor, setActiveColor] = useState(0)
  const [message, setMessage] = useState('')
  const effectTimerRef = useRef<number | null>(null)
  const messageTimerRef = useRef<number | null>(null)

  async function refresh(includePrograms = false) {
    const [s, p] = await Promise.all([
      fetch('/status').then((r) => r.json()),
      fetch('/peers').then((r) => r.json()).catch(() => []),
    ])
    setStatus(s)
    setPeers(p)
    if (!includePrograms) return
    const pr = await fetch('/programs').then((r) => r.json()).catch(() => null)
    if (pr) {
      setPrograms(pr)
      if (pr.selected_slot) setSelectedSlot(pr.selected_slot)
      else if (pr.files?.length && selectedSlot > pr.files.length) setSelectedSlot(1)
    }
  }

  function showMessage(text: string, clearAfterMs = 0) {
    setMessage(text)
    if (messageTimerRef.current !== null) window.clearTimeout(messageTimerRef.current)
    if (clearAfterMs > 0) {
      messageTimerRef.current = window.setTimeout(() => {
        messageTimerRef.current = null
        setMessage('')
      }, clearAfterMs)
    }
  }

  useEffect(() => {
    fetch('/config')
      .then((r) => r.json())
      .then((d: Config) => {
        setConfig(d)
        setEffect(readEffect(d))
      })
    refresh(true)
    const id = setInterval(() => refresh(false), 2000)
    return () => {
      clearInterval(id)
      if (effectTimerRef.current !== null) window.clearTimeout(effectTimerRef.current)
      if (messageTimerRef.current !== null) window.clearTimeout(messageTimerRef.current)
    }
  }, [])

  function applyEffect(next: EffectState, immediate = false) {
    setEffect(next)
    if (effectTimerRef.current !== null) window.clearTimeout(effectTimerRef.current)

    const send = () => {
      effectTimerRef.current = null
      postJson('/effect', effectPayload(next, immediate)).then(() => refresh(false)).catch(() => showMessage('Effect update failed'))
    }

    if (immediate) send()
    else effectTimerRef.current = window.setTimeout(send, 180)
  }

  function patchEffect(patch: Partial<EffectState>, immediate = false) {
    const next = { ...effect, ...patch }
    const slots = colorSlotsForEffect(next.effectId)
    if (activeColor >= slots) setActiveColor(Math.max(0, slots - 1))
    applyEffect(next, immediate)
  }

  function choosePalette(id: number) {
    const palette = PALETTES.find((p) => p.id === id) ?? PALETTES[0]
    applyEffect({ ...effect, paletteId: id, colors: palette.colors }, true)
  }

  function setColor(index: number, color: Color) {
    const colors = [...effect.colors]
    colors[index] = color
    applyEffect({ ...effect, paletteId: 0, colors })
  }

  function setBrightness(value: number) {
    fetch(`/brightness?v=${value}`).then(() => refresh(false))
    setConfig((prev) => prev ? { ...prev, brightness: value } : prev)
  }

  function uploadFiles(files: FileList | null) {
    if (!files || !programs) return
    const selected = Array.from(files)
    const total = selected.reduce((sum, file) => sum + file.size, 0)
    if (total > programs.free) {
      showMessage(`Not enough space. Free: ${fmtBytes(programs.free)}`)
      return
    }
    showMessage('Uploading...')
    selected.reduce((promise, file) => promise.then(async () => {
      const fd = new FormData()
      fd.append('file', file, file.name)
      const res = await fetch('/program/upload', { method: 'POST', body: fd })
      if (!res.ok) throw new Error(await res.text())
    }), Promise.resolve())
      .then(() => { showMessage('Upload complete', 1500); refresh(true) })
      .catch((err) => showMessage(err.message || 'Upload failed'))
  }

  function selectProgramSlot(slot: number) {
    setSelectedSlot(slot)
    if (programs?.files.some((file) => file.slot === slot)) postJson('/program/select', { slot }).then(() => refresh(true))
  }

  function startProgram() {
    postJson('/program/start', { slot: selectedSlot }).then(() => refresh(false))
  }

  function stopProgram() {
    fetch('/stop').then(() => refresh(false)).catch(() => showMessage('Stop failed'))
  }

  function deleteProgram(name: string) {
    postJson('/program/delete', { file: name }).then(() => { showMessage('Deleted', 1500); refresh(true) })
  }

  function reorderProgram(slot: number, direction: -1 | 1) {
    postJson('/program/reorder', { slot, direction }).then(() => refresh(true))
  }

  function togglePower() {
    postJson('/power', { on: !(status?.power_on ?? status?.playing ?? false) }).then(() => refresh(false))
  }

  function saveSettings(patch: Partial<Config>) {
    const next = { ...config, ...patch }
    setConfig(next)
    postJson('/config', patch).then((text) => {
      showMessage(text, 1800)
      refresh(false)
    })
  }

  function setSyncEnabled(enabled: boolean) {
    const mask = syncMask || 1
    setConfig((prev) => prev ? { ...prev, syncEnabled: enabled, syncMask: mask } : prev)
    postJson('/config', { syncEnabled: enabled, syncMask: mask }).then(() => refresh(false))
  }

  const meta = EFFECTS.find((e) => e.id === effect.effectId) ?? EFFECTS[0]
  const sizeMax = meta.sizeMax ?? 32
  const syncMask = config?.syncMask ?? status?.sync_mask ?? 0
  const syncEnabled = config?.syncEnabled ?? status?.sync_enabled ?? false
  const powerOn = status?.power_on ?? status?.playing ?? false
  const selectedProgram = programs?.files.find((file) => file.slot === selectedSlot)
  const colorSlotCount = colorSlotsForEffect(effect.effectId)
  const activeColorValue = effect.colors[activeColor] ?? effect.colors[0] ?? DEFAULT_EFFECT.colors[0]
  const activeHsv = colorToHsv(activeColorValue)
  const brightnessValue = (config?.brightness ?? 100) > 0 ? (config?.brightness ?? 100) : 100

  return (
    <div class="app-shell">
      <header class="topbar">
        <div class="device-title">
          <div class="brand">AuraX</div>
          <div class="device-name">{status?.device_name ?? status?.hostname ?? config?.deviceName ?? config?.hostname ?? 'aurax'}</div>
        </div>
        <div class="header-actions">
          <div class="header-metrics">
            <span>IP {status?.ip ?? '-'}</span>
            <span><WifiIcon /> {rssiPercent(status?.rssi) !== null ? `${rssiPercent(status?.rssi)}%` : '-'}</span>
            <span>{fmtFps(status)}</span>
            <span><BatteryIcon /> {status?.battery_pct ?? 0}%</span>
          </div>
          <button class={syncEnabled ? 'sync-toggle active' : 'sync-toggle'} onClick={() => setSyncEnabled(!syncEnabled)}>
            SYNC
          </button>
          <button class={powerOn ? 'power-button active' : 'power-button'} onClick={togglePower} title={powerOn ? 'Off' : 'On'} aria-label={powerOn ? 'Off' : 'On'}>
            <PowerIcon />
          </button>
        </div>
      </header>

      <nav class="tabs">
        {(['programs', 'colors', 'effects', 'settings'] as Tab[]).map((name) => (
          <button class={tab === name ? 'active' : ''} onClick={() => setTab(name)}>{name[0].toUpperCase() + name.slice(1)}</button>
        ))}
      </nav>

      <main>
        {tab === 'programs' && (
          <section class="panel">
            <div class="section-head">
              <h2>Programs</h2>
              <span>{programs ? `${fmtBytes(programs.free)} free` : 'Loading'}</span>
            </div>
            <div class="start-row">
              <button class="start-button" disabled={!selectedProgram} onClick={startProgram}>START</button>
              <button class="stop-button" onClick={stopProgram}>STOP</button>
              <div class="program-picker">
                {Array.from({ length: 5 }, (_, i) => i + 1).map((slot) => (
                  <button class={selectedSlot === slot ? 'active' : ''} onClick={() => selectProgramSlot(slot)}>
                    {slot}
                  </button>
                ))}
              </div>
            </div>
            <label class="upload-zone">
              <input type="file" accept=".pix,.axp" multiple onChange={(e) => uploadFiles((e.currentTarget as HTMLInputElement).files)} />
              <strong>Upload program files</strong>
              <span>Photon .pix and AuraX .axp files are checked against remaining LittleFS space before upload.</span>
            </label>
            <div class="program-list">
              {(programs?.files ?? []).map((file) => (
                <div class={file.slot === selectedSlot ? 'program active' : 'program'} key={file.name} onClick={() => selectProgramSlot(file.slot)}>
                  <b class="program-slot">{String(file.slot).padStart(3, '0')}</b>
                  <div>
                    <strong>{file.display_name ?? file.name.replace(/^\//, '')}</strong>
                    <span>{fmtBytes(file.size)}</span>
                  </div>
                  <div class="row-actions">
                    <button disabled={file.slot === 1} onClick={(e) => { e.stopPropagation(); reorderProgram(file.slot, -1) }} title="Move up" aria-label="Move up">Up</button>
                    <button disabled={file.slot === programs?.files.length} onClick={(e) => { e.stopPropagation(); reorderProgram(file.slot, 1) }} title="Move down" aria-label="Move down">Down</button>
                    <button class="danger" onClick={(e) => { e.stopPropagation(); deleteProgram(file.name) }}>Delete</button>
                  </div>
                </div>
              ))}
              {programs && programs.files.length === 0 && <p class="empty">No program uploaded yet.</p>}
            </div>
          </section>
        )}

        {tab === 'colors' && (
          <section class="panel">
            <div class="section-head">
              <h2>Colors</h2>
              <span>{PALETTES.find((p) => p.id === effect.paletteId)?.name ?? 'Custom'}</span>
            </div>
            <div class="color-layout">
              <div class="color-left">
                {colorSlotCount > 0 && (
                  <ColorWheel color={activeColorValue} onChange={(color) => setColor(activeColor, color)} />
                )}
              </div>
              <div class="color-slots">
                {effect.colors.slice(0, colorSlotCount).map((color, i) => (
                  <button
                    class={activeColor === i ? 'slot active' : 'slot'}
                    style={`--swatch:${colorToHex(color)}`}
                    onClick={() => setActiveColor(i)}
                  >
                    Color {i + 1}
                  </button>
                ))}
              </div>
              <div class="color-adjustments">
                {colorSlotCount > 0 && (
                  <label class="value-control">
                    <span>Value</span>
                    <input type="range" min={0} max={100} value={Math.round(activeHsv.v * 100)}
                      onInput={(e) => setColor(activeColor, hsvToColor(activeHsv.h, activeHsv.s, +(e.currentTarget as HTMLInputElement).value / 100))} />
                    <b>{Math.round(activeHsv.v * 100)}%</b>
                  </label>
                )}
                <label class="brightness">
                  <span>LED brightness</span>
                  <input type="range" min={1} max={100} value={brightnessValue}
                    onInput={(e) => setBrightness(+(e.currentTarget as HTMLInputElement).value)} />
                  <b>{brightnessValue}%</b>
                </label>
              </div>
            </div>
            <h3>Color Palettes</h3>
            <div class="palette-grid">
              {PALETTES.map((palette) => (
                <button class={effect.paletteId === palette.id ? 'palette active' : 'palette'} onClick={() => choosePalette(palette.id)}>
                  <span>{palette.name}</span>
                  <i style={`background:linear-gradient(90deg,${palette.colors.map(colorToHex).join(',')})`} />
                </button>
              ))}
            </div>
          </section>
        )}

        {tab === 'effects' && (
          <section class="panel">
            <div class="section-head">
              <h2>Effects</h2>
              <span>{meta.name}</span>
            </div>
            <div class="effect-list">
              {EFFECTS.map((fx) => (
                <button class={effect.effectId === fx.id ? 'effect active' : 'effect'} onClick={() => patchEffect({ effectId: fx.id }, true)}>
                  {fx.name}
                </button>
              ))}
            </div>
            <div class="controls">
              {meta.speed && <Slider label={meta.speed} value={effect.speed} min={10} max={1000} onInput={(value) => patchEffect({ speed: value })} />}
              {meta.intensity && <Slider label={meta.intensity} value={effect.intensity} min={0} max={255} onInput={(value) => patchEffect({ intensity: value })} />}
              {meta.size && <Slider label={meta.size} value={Math.min(effect.size, sizeMax)} min={1} max={sizeMax} onInput={(value) => patchEffect({ size: value })} />}
            </div>
          </section>
        )}

        {tab === 'settings' && config && (
          <SettingsPanel
            config={config}
            status={status}
            syncMask={syncMask}
            syncEnabled={syncEnabled}
            saveSettings={saveSettings}
            setMessage={setMessage}
          />
        )}
      </main>

      {message && <div class="toast" onClick={() => setMessage('')}>{message}</div>}

      <footer class="device-list">
        <h3>Devices</h3>
        <a class="device-row" href={`http://${status?.ip ?? ''}/`}>
          <div>
            <b>{status?.device_name ?? status?.hostname ?? 'Local'}</b>
            <span>{status?.ip ?? '-'}</span>
          </div>
          <i><BatteryIcon /> {status?.battery_pct ?? 0}%</i>
          <i><WifiIcon /> {rssiPercent(status?.rssi) !== null ? `${rssiPercent(status?.rssi)}%` : '-'}</i>
        </a>
        {peers.map((peer) => (
          <a class="device-row" href={`http://${peer.ip}/`}>
            <div>
              <b>{peer.hostname}</b>
              <span>{peer.ip}</span>
            </div>
            <i><BatteryIcon /> {peer.bat_pct}%</i>
            <i><WifiIcon /> {rssiPercent(peer.rssi) !== null ? `${rssiPercent(peer.rssi)}%` : '-'}</i>
          </a>
        ))}
      </footer>
    </div>
  )
}

function PowerIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M12 2v10" />
      <path d="M18.4 6.6a9 9 0 1 1-12.8 0" />
    </svg>
  )
}

function WifiIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M5 12.5a10 10 0 0 1 14 0" />
      <path d="M8.5 16a5 5 0 0 1 7 0" />
      <path d="M12 20h.01" />
    </svg>
  )
}

function BatteryIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M3 8h15a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2H3z" />
      <path d="M20 11h2v2h-2" />
    </svg>
  )
}

function ReverseIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M17 7H5" />
      <path d="M9 3 5 7l4 4" />
      <path d="M7 17h12" />
      <path d="m15 13 4 4-4 4" />
    </svg>
  )
}

function MirrorIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M12 3v18" />
      <path d="M8 7 4 12l4 5" />
      <path d="m16 7 4 5-4 5" />
    </svg>
  )
}

function ColorWheel({ color, onChange }: { color: Color; onChange: (color: Color) => void }) {
  const hsv = colorToHsv(color)
  const angle = hsv.h * Math.PI / 180
  const radius = hsv.s * 50
  const x = 50 + Math.cos(angle) * radius
  const y = 50 + Math.sin(angle) * radius

  function pick(e: PointerEvent, element: HTMLDivElement) {
    const rect = element.getBoundingClientRect()
    const cx = rect.left + rect.width / 2
    const cy = rect.top + rect.height / 2
    const dx = e.clientX - cx
    const dy = e.clientY - cy
    const max = rect.width / 2
    const dist = Math.min(Math.sqrt(dx * dx + dy * dy), max)
    const hue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360
    const saturation = Math.min(1, dist / max)
    onChange(hsvToColor(hue, saturation, hsv.v))
  }

  return (
    <div class="color-control">
      <div
        class="color-wheel"
        style={`--x:${x}%;--y:${y}%;--swatch:${colorToHex(color)}`}
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId)
          pick(e, e.currentTarget)
        }}
        onPointerMove={(e) => {
          if (e.buttons) pick(e, e.currentTarget)
        }}
      >
        <span />
      </div>
    </div>
  )
}

function Slider({ label, value, min, max, onInput }: { label: string; value: number; min: number; max: number; onInput: (value: number) => void }) {
  return (
    <label class="slider">
      <span>{label}</span>
      <input type="range" min={min} max={max} value={value}
        onInput={(e) => onInput(+(e.currentTarget as HTMLInputElement).value)} />
      <b>{value}</b>
    </label>
  )
}

function SettingsPanel({
  config,
  status,
  syncMask,
  syncEnabled,
  saveSettings,
  setMessage,
}: {
  config: Config
  status: StatusResponse | null
  syncMask: number
  syncEnabled: boolean
  saveSettings: (patch: Partial<Config>) => void
  setMessage: (message: string) => void
}) {
  const [form, setForm] = useState({
    hostname: config.deviceName ?? config.hostname ?? 'aurax',
    ssid: config.ssid ?? '',
    password: config.password ?? '',
    ledType: config.ledType ?? 1,
    numLeds: config.numLeds ?? 144,
    dataPin: config.dataPin ?? 6,
    clkPin: config.clkPin ?? 5,
    mALimit: config.mALimit ?? 0,
    effectReverse: config.effectReverse ?? false,
    renderMirror: config.renderMirror ?? false,
    syncEnabled,
    syncMask,
    batPin: config.batPin ?? 8,
    batMultiplier: config.batMultiplier ?? 2.904,
    batCalibration: config.batCalibration ?? 0.344,
    batMinMv: config.batMinMv ?? 3000,
    batMaxMv: config.batMaxMv ?? 4200,
    batAutoOff: config.batAutoOff ?? false,
    batAutoOffThreshold: config.batAutoOffThreshold ?? 10,
  })
  const set = (patch: Partial<typeof form>) => setForm((prev) => ({ ...prev, ...patch }))

  function updateFw(e: Event) {
    const input = e.currentTarget as HTMLInputElement
    const file = input.files?.[0]
    if (!file) return
    const fd = new FormData()
    fd.append('firmware', file, file.name)
    const xhr = new XMLHttpRequest()
    setMessage('Firmware upload starting...')
    xhr.upload.onprogress = (event) => {
      if (!event.lengthComputable) {
        setMessage('Firmware uploading...')
        return
      }
      setMessage(`Firmware upload ${Math.round((event.loaded / event.total) * 100)}%`)
    }
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) setMessage('Firmware uploaded, device rebooting...')
      else setMessage(xhr.responseText || 'Firmware upload failed')
      input.value = ''
    }
    xhr.onerror = () => {
      setMessage('Firmware upload failed')
      input.value = ''
    }
    xhr.open('POST', '/update')
    xhr.send(fd)
  }

  function formatStorage() {
    if (!window.confirm('Format storage? This removes all programs and settings.')) return
    setMessage('Formatting storage...')
    fetch('/storage/format', { method: 'POST' })
      .then((res) => res.text().then((text) => {
        if (!res.ok) throw new Error(text || 'Storage format failed')
        setMessage(text || 'Storage formatted, device rebooting...')
      }))
      .catch((err) => setMessage(err.message || 'Storage format failed'))
  }

  return (
    <section class="panel settings">
      <div class="section-head">
        <h2>Settings</h2>
        <button onClick={() => saveSettings(form)}>Save</button>
      </div>
      <div class="settings-basic">
        <label class="full">Device name<input value={form.hostname} onInput={(e) => set({ hostname: (e.currentTarget as HTMLInputElement).value })} /></label>
        <label>WiFi SSID<input value={form.ssid} onInput={(e) => set({ ssid: (e.currentTarget as HTMLInputElement).value })} /></label>
        <label>Password<input type="password" value={form.password} onInput={(e) => set({ password: (e.currentTarget as HTMLInputElement).value })} /></label>
      </div>

      <h3>Rendering</h3>
      <div class="render-modes">
        <button
          type="button"
          class={form.effectReverse ? 'mode-button active' : 'mode-button'}
          onClick={() => set({ effectReverse: !form.effectReverse })}
        >
          <ReverseIcon /> Reverse rendering
        </button>
        <button
          type="button"
          class={form.renderMirror ? 'mode-button active' : 'mode-button'}
          onClick={() => set({ renderMirror: !form.renderMirror })}
        >
          <MirrorIcon /> Center mirror
        </button>
      </div>

      <h3>Sync channels</h3>
      <label class="check"><input type="checkbox" checked={form.syncEnabled} onChange={(e) => set({ syncEnabled: (e.currentTarget as HTMLInputElement).checked, syncMask: form.syncMask || 1 })} /> Enable sync</label>
      <div class={form.syncEnabled ? 'sync-classes' : 'sync-classes disabled'}>
        {Array.from({ length: 10 }, (_, i) => i + 1).map((ch) => {
          const bit = 1 << (ch - 1)
          return (
            <label class={form.syncMask & bit ? 'checked' : ''}>
              <input type="checkbox" checked={!!(form.syncMask & bit)}
                onChange={(e) => set({ syncMask: (e.currentTarget as HTMLInputElement).checked ? form.syncMask | bit : form.syncMask & ~bit })} />
              {ch}
            </label>
          )
        })}
      </div>

      <div class="firmware-row">
        <button onClick={() => fetch('/reboot', { method: 'POST' })}>Reboot</button>
        <label class="fw-button">Firmware<input type="file" accept=".bin" onChange={updateFw} /></label>
      </div>
      <details class="advanced-settings">
        <summary>Advanced</summary>
        <h3>Storage</h3>
        <div class="firmware-row">
          <button type="button" onClick={formatStorage}>Format storage</button>
        </div>

        <h3>LED output</h3>
        <div class="form-grid">
          <label>LED type<select value={form.ledType} onChange={(e) => set({ ledType: +(e.currentTarget as HTMLSelectElement).value })}>
            <option value={1}>APA102</option>
            <option value={0}>WS281x</option>
          </select></label>
          <label>LED count<input type="number" value={form.numLeds} min={1} max={2048} onInput={(e) => set({ numLeds: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label>Data pin<input type="number" value={form.dataPin} min={0} max={48} onInput={(e) => set({ dataPin: +(e.currentTarget as HTMLInputElement).value })} /></label>
          {form.ledType === 1 && (
            <label>Clock pin<input type="number" value={form.clkPin} min={0} max={48} onInput={(e) => set({ clkPin: +(e.currentTarget as HTMLInputElement).value })} /></label>
          )}
          <label>Current limit mA<input type="number" value={form.mALimit} min={0} max={65000} step={100} onInput={(e) => set({ mALimit: +(e.currentTarget as HTMLInputElement).value })} /></label>
        </div>

        <h3>Battery</h3>
        <div class="live-metrics">
          <span><BatteryIcon /> {status?.battery_mv ? (status.battery_mv / 1000).toFixed(3) : '-'} V</span>
          <span><WifiIcon /> {rssiPercent(status?.rssi) !== null ? `${rssiPercent(status?.rssi)}%` : '-'}</span>
          <span>{fmtFps(status)}</span>
        </div>
        <div class="form-grid">
          <label>ADC pin<input type="number" value={form.batPin} min={0} max={48} onInput={(e) => set({ batPin: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label>Multiplier<input type="number" value={form.batMultiplier} step={0.001} onInput={(e) => set({ batMultiplier: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label>Calibration V<input type="number" value={form.batCalibration} step={0.001} onInput={(e) => set({ batCalibration: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label>Min mV<input type="number" value={form.batMinMv} onInput={(e) => set({ batMinMv: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label>Max mV<input type="number" value={form.batMaxMv} onInput={(e) => set({ batMaxMv: +(e.currentTarget as HTMLInputElement).value })} /></label>
          <label class="check"><input type="checkbox" checked={form.batAutoOff} onChange={(e) => set({ batAutoOff: (e.currentTarget as HTMLInputElement).checked })} /> Auto off</label>
          <label>Auto off %<input type="number" min={0} max={100} value={form.batAutoOffThreshold} onInput={(e) => set({ batAutoOffThreshold: +(e.currentTarget as HTMLInputElement).value })} /></label>
        </div>
      </details>
    </section>
  )
}
