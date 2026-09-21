import { AudioLines, Mic, Music2, Play, Square, RotateCcw } from 'lucide-preact'
import { AUDIO_EFFECTS, audioSensitivityGain, audioReleaseMs } from './audio-reactive'
import type { AudioEffectSettings, AudioSource, AudioState } from './audio-reactive'

type Props = {
  packetRate?: number; renderFps?: number; groupRole?: string; groupChannel?: number; syncEnabled?: boolean
  leaveGroup: () => void
  state: AudioState; source: AudioSource; sensitivity: number; smoothing: number; effect: AudioEffectSettings
  setSource: (source: AudioSource) => void; setSensitivity: (value: number) => void; setSmoothing: (value: number) => void
  selectEffect: (mode: number) => void; setEffect: (settings: AudioEffectSettings) => void
  start: () => void; stop: () => void; reset: () => void
}

function Control({ label, value, min = 0, max = 100, display, onInput }: {
  label: string; value: number; min?: number; max?: number; display?: string; onInput: (value: number) => void
}) {
  return <label class="audio-control"><span>{label}</span>
    <input type="range" min={min} max={max} value={value} onInput={e => onInput(+e.currentTarget.value)} />
    <output>{display ?? value}</output></label>
}

export function AudioPanel(p: Props) {
  const fx = AUDIO_EFFECTS[p.effect.mode]
  const running = p.state.active || p.state.starting
  const patch = (values: Partial<AudioEffectSettings>) => p.setEffect({ ...p.effect, ...values })
  const levels = p.state.levels
  const drivingLevel = p.state.active ? [levels.volume, levels.bass, levels.mid, levels.treble][p.effect.response] : 0
  if (p.groupRole === 'receiver' && !p.state.active) return <section class="panel audio-panel">
    <div class="section-head audio-heading"><h2><AudioLines size={22} />Audio Reactive</h2><span class="audio-live active">SYNC receiver</span></div>
    <button class="audio-run active" onClick={p.leaveGroup}><Square size={17} />Stop receiving</button>
    <div class="audio-stream-status"><span>ESP-NOW / channel {p.groupChannel ?? '?'}</span>
      {p.renderFps !== undefined && <span>LED: {Math.round(p.renderFps)} FPS</span>}</div>
  </section>
  return <section class="panel audio-panel">
    <div class="section-head audio-heading"><h2><AudioLines size={22} />Audio Reactive</h2>
      <span class={p.state.active ? 'audio-live active' : 'audio-live'}>{p.state.starting ? 'Connecting' : p.state.active ? 'Live' : 'Stopped'}</span></div>
    <div class="audio-input-row">
      <label class="audio-source"><span>Source</span><select value={p.source} disabled={running}
        onChange={e => p.setSource(e.currentTarget.value as AudioSource)}>
        <option value="microphone">Microphone</option>
        <option value="playback" disabled={!p.state.playback}>{p.state.native ? 'Device audio' : 'Tab / system audio'}</option>
      </select></label>
      <button class={running ? 'audio-run active' : 'audio-run'} onClick={running ? p.stop : p.start}>
        {running ? <Square size={17} /> : <Play size={17} />}{p.state.starting ? 'Cancel' : p.state.active ? 'Stop' : 'Start'}</button>
    </div>
    {p.source === 'playback' && <p class="audio-note"><Music2 size={14} />Some apps block internal audio capture.</p>}
    {!p.state.playback && <p class="audio-note"><Mic size={14} />Microphone only on this platform.</p>}
    <label class="audio-mirror" title="Uses your enabled SYNC channels and compatible receiver firmware">
      <input type="checkbox" checked={p.effect.group} disabled={!p.syncEnabled && !p.effect.group}
        onChange={e => patch({ group: e.currentTarget.checked })} />SYNC group
    </label>
    <div class="audio-levels" aria-label="Audio input levels">
      {(['volume', 'bass', 'mid', 'treble'] as const).map(name => <div key={name}><span>{name}</span>
        <meter min={0} max={255} value={p.state.active ? levels[name] : 0} aria-label={name} /></div>)}
      <div class={levels.beat > 0 && p.state.active ? 'audio-beat active' : 'audio-beat'} aria-label="Beat"><i />Beat</div>
    </div>
    <div class="audio-controls input-controls">
      <Control label="Sensitivity" value={p.sensitivity} min={0} max={300} display={audioSensitivityGain(p.sensitivity).toFixed(2) + 'x'} onInput={p.setSensitivity} />
      <Control label="Smoothing" value={p.smoothing} max={95} display={Math.round(audioReleaseMs(p.smoothing)) + ' ms'} onInput={p.setSmoothing} />
    </div>
    {p.state.spectrum && <div class="audio-spectrum" role="img" aria-label="Live frequency spectrum">
      {p.state.spectrum.map((value, bin) => <i key={bin} style={{ height: (p.state.active ? value / 255 * 100 : 0) + '%' }} />)}
    </div>}
    {p.state.active && p.state.transport && <div class="audio-stream-status">
      {p.groupRole === 'sender' && <span>Group broadcast / channel {p.groupChannel ?? '?'}</span>}
      <span>{p.state.transport === 'udp2' ? 'UDP / 64 bands' : 'Legacy HTTP / 3 bands'}</span>
      {p.state.analysisRate !== undefined && <span>Analysis: {Math.round(p.state.analysisRate)} /s</span>}
      {p.state.sentRate !== undefined && <span>Sent: {Math.round(p.state.sentRate)} /s</span>}
      {p.packetRate !== undefined && p.state.transport === 'udp2' && <span>Received: {Math.round(p.packetRate)} /s</span>}
      {p.renderFps !== undefined && <span>LED: {Math.round(p.renderFps)} FPS</span>}
      {(p.state.roundTripMs ?? -1) >= 0 && <span title="Analyzed frame to acknowledgement, not total audio-to-light latency">Stream ACK: {p.state.roundTripMs} ms</span>}
    </div>}
    <h3>Audio effects</h3>
    <div class="audio-effect-grid" aria-label="Audio effects">
      {AUDIO_EFFECTS.map((effect, mode) => <button key={mode} aria-pressed={mode === p.effect.mode}
        class={mode === p.effect.mode ? 'active' : ''} onClick={() => p.selectEffect(mode)}>
        <span class="audio-effect-number" aria-hidden="true">{String(mode + 1).padStart(2, '0')}</span>{effect.name}</button>)}
    </div>
    <div class="audio-effect-heading"><h3>{fx.name}</h3><button class="audio-reset" title="Reset this audio effect" aria-label="Reset this audio effect" onClick={p.reset}><RotateCcw size={17} /></button></div>
    <label class="audio-frequency"><span>React to</span>
      <select value={p.effect.response} onChange={e => patch({ response: +e.currentTarget.value })}>
        <option value={0}>Full spectrum</option><option value={1}>Bass</option>
        <option value={2}>Mids</option><option value={3}>Treble</option>
      </select>
    </label>
    <div class="audio-driving-level"><span>Driving level</span>
      <meter min={0} max={255} value={drivingLevel} aria-label="Selected frequency range level" />
      <output>{Math.round(drivingLevel / 255 * 100)}%</output></div>
    <div class="audio-controls">
      <Control label={fx.speed} value={p.effect.speed} onInput={speed => patch({ speed })} />
      <Control label={fx.width} value={p.effect.width} onInput={width => patch({ width })} />
      {fx.decay && <Control label="Decay" value={p.effect.decay} onInput={decay => patch({ decay })} />}
      <Control label="Brightness" value={p.effect.brightness} onInput={brightness => patch({ brightness })} />
    </div>
    <div class="audio-colors"><span>Colors</span>{p.effect.colors.map((color, index) => <label title={`Color ${index + 1}`} key={index}>
      <input type="color" value={color} aria-label={`Audio color ${index + 1}`} onInput={e => patch({ colors: p.effect.colors.map((old, n) => n === index ? e.currentTarget.value : old) })} />
    </label>)}<label class="audio-mirror"><input type="checkbox" checked={p.effect.mirror} onChange={e => patch({ mirror: e.currentTarget.checked })} />Mirror</label></div>
  </section>
}
