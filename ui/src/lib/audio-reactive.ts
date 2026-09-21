export type AudioSource = 'microphone' | 'playback'
export type AudioLevels = { volume: number; bass: number; mid: number; treble: number; beat: number }
export const SILENCE: AudioLevels = { volume: 0, bass: 0, mid: 0, treble: 0, beat: 0 }
export const AUDIO_ENTRY_NOTICE = 'Audio Reactive on your phone requires the AuraX Finder app.'
export const AUDIO_ENTRY_NOTICE_MS = 4000
export function audioSensitivityGain(value: number) { return Math.pow(Math.max(0, Math.min(300, value)) / 130, 2) }
export function audioReleaseMs(value: number) { return 600 * Math.pow(Math.max(0, Math.min(95, value)) / 95, 2) }
export function audioByteLevel(value: number) {
  if (!Number.isFinite(value) || value <= 0) return 0
  const shaped = value <= 0.55 ? value : 0.55 + 0.45 * (1 - 0.45 / (value - 0.10))
  return Math.min(255, Math.round(shaped * 255))
}
export type AudioEffectSettings = {
  mode: number; speed: number; width: number; decay: number; brightness: number; response: number; mirror: boolean; colors: string[]; group: boolean
}
export const AUDIO_EFFECTS = [
  { name: 'Spectrum', speed: 'Color speed', width: 'Envelope blend', decay: true },
  { name: 'VU Meter', speed: 'Peak fall', width: 'Peak width', decay: false },
  { name: 'Bass Pulse', speed: 'Color speed', width: 'Pulse width', decay: true },
  { name: 'Beat Ripple', speed: 'Expansion', width: 'Ring width', decay: true },
  { name: 'Waves', speed: 'Wave speed', width: 'Wavelength', decay: false },
  { name: 'Sparkles', speed: 'Activity', width: 'Spark size', decay: true },
  { name: 'Comet', speed: 'Travel speed', width: 'Tail length', decay: true },
  { name: 'Color Flow', speed: 'Flow speed', width: 'Trail length', decay: false },
  { name: 'Beat Steps', speed: 'Beat decay', width: 'Block size', decay: false },
  { name: 'Fire', speed: 'Turbulence', width: 'Flame size', decay: false },
] as const

export function readAudioEffect(mode?: number): AudioEffectSettings {
  const selected = mode ?? readAudioSetting('aurax-audio-mode', 0, 0, 9)
  const defaults: AudioEffectSettings = { mode: selected, speed: 50, width: 35, decay: 50, brightness: 80,
    response: 0, group: readAudioSetting('aurax-audio-group', 0, 0, 1) === 1, mirror: false, colors: selected === 9 ? ['#ff2400', '#ff9100', '#fff080'] : ['#ff6000', '#00dcd2', '#ff2070'] }
  try {
    const saved = JSON.parse(localStorage.getItem(`aurax-audio-effect-${selected}`) ?? 'null')
    if (!saved || typeof saved !== 'object') return defaults
    for (const key of ['speed', 'width', 'decay', 'brightness'] as const) {
      if (Number.isInteger(saved[key]) && saved[key] >= 0 && saved[key] <= 100) defaults[key] = saved[key]
    }
    if (typeof saved.mirror === 'boolean') defaults.mirror = saved.mirror
    if (Number.isInteger(saved.response) && saved.response >= 0 && saved.response <= 3) defaults.response = saved.response
    if (Array.isArray(saved.colors) && saved.colors.length === 3 && saved.colors.every((c: unknown) => typeof c === 'string' && /^#[0-9a-f]{6}$/i.test(c))) defaults.colors = saved.colors
  } catch { /* Storage can be unavailable in embedded browsers. */ }
  return defaults
}

export function saveAudioEffect(settings: AudioEffectSettings) {
  try {
    localStorage.setItem('aurax-audio-mode', String(settings.mode))
    localStorage.setItem('aurax-audio-group', settings.group ? '1' : '0')
    localStorage.setItem(`aurax-audio-effect-${settings.mode}`, JSON.stringify(settings))
  } catch { /* Audio remains usable without persistent browser storage. */ }
}

export function releaseEnvelope(previous: number, value: number, smoothing: number, elapsedMs = 25) {
  const ms = audioReleaseMs(smoothing)
  const release = ms === 0 ? 0 : Math.exp(-Math.max(1, elapsedMs) / ms)
  return value >= previous ? value : previous * release + value * (1 - release)
}
type Bridge = { postMessage(message: string): void; onmessage?: (event: MessageEvent) => void }
type AudioWindow = Window & {
  AuraXAudio?: Bridge
  webkit?: { messageHandlers?: { AuraXAudio?: Bridge } }
  webkitAudioContext?: typeof AudioContext
}
type NativeEvent = {
  type: 'capabilities' | 'state'
  playback?: boolean
  session?: string
  active?: boolean
  starting?: boolean
  error?: string
  levels?: AudioLevels
  spectrum?: number[]
  transport?: string
  roundTripMs?: number
  analysisRate?: number; sentRate?: number
}
export type AudioState = {
  active: boolean; starting: boolean; native: boolean; playback: boolean; levels: AudioLevels
  spectrum?: number[]; transport?: string; roundTripMs?: number; analysisRate?: number; sentRate?: number
}

export function readAudioSetting(key: string, fallback: number, min: number, max: number): number {
  try {
    const raw = localStorage.getItem(key)
    const value = raw === null ? NaN : Number(raw)
    return Number.isInteger(value) && value >= min && value <= max ? value : fallback
  } catch { return fallback }
}

export function encodeAudioFrame(levels: AudioLevels): string {
  return [1, levels.volume, levels.bass, levels.mid, levels.treble, levels.beat]
    .map((value) => Math.max(0, Math.min(255, Math.round(Number.isFinite(value) ? value : 0))).toString(16).padStart(2, '0')).join('')
}

async function request(url: string, body = '') {
  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), 1000)
  try {
    const response = await fetch(url, {
      method: 'POST', body, headers: { 'Content-Type': 'text/plain' }, signal: controller.signal, cache: 'no-store',
    })
    if (!response.ok) throw new Error((await response.text()) || `Request failed (${response.status})`)
  } finally { clearTimeout(timeout) }
}

export class AudioReactiveController {
  private bridge?: Bridge
  private session = ''
  private source: AudioSource = 'microphone'
  private stream?: MediaStream
  private context?: AudioContext
  private timer?: number
  private permissionTimer?: number
  private sensitivity = 130
  private smoothing = 25
  private effect = readAudioEffect()
  private effectRevision = 0
  private effectSent = -1
  private effectBusy = false
  private effectTimer?: number
  state: AudioState = { active: false, starting: false, native: false, playback: false, levels: SILENCE }

  constructor(private changed: (state: AudioState) => void, private error: (message: string) => void) {
    const win = window as AudioWindow
    this.bridge = win.AuraXAudio ?? win.webkit?.messageHandlers?.AuraXAudio
    this.state.native = !!this.bridge
    this.state.playback = !this.bridge && !!navigator.mediaDevices?.getDisplayMedia && window.isSecureContext
    if (win.AuraXAudio) win.AuraXAudio.onmessage = (event) => {
      try { this.receive(JSON.parse(event.data)) } catch { /* Ignore unrelated/invalid bridge messages. */ }
    }
    window.addEventListener('aurax-audio', this.onNative)
    window.addEventListener('pagehide', this.onPageHide)
    document.addEventListener('visibilitychange', this.onVisibility)
    this.bridge?.postMessage(JSON.stringify({ action: 'capabilities' }))
    this.publish()
  }

  private publish() { this.changed({ ...this.state }) }
  private onNative = (event: Event) => this.receive((event as CustomEvent<NativeEvent>).detail)
  private receive(data: NativeEvent) {
    if (data.type === 'capabilities') {
      this.state.playback = data.playback === true
      this.publish()
    } else if (data.type === 'state' && data.session === this.session) {
      if (data.starting) return
      clearTimeout(this.permissionTimer)
      this.state.active = data.active === true
      this.state.starting = false
      this.state.levels = data.levels ?? SILENCE
      this.state.spectrum = data.spectrum?.length === 64 ? data.spectrum.map(n => Math.max(0, Math.min(255, Number(n) || 0))) : undefined
      this.state.transport = data.transport
      this.state.roundTripMs = data.roundTripMs
      this.state.analysisRate = Number.isFinite(data.analysisRate) ? data.analysisRate : undefined
      this.state.sentRate = Number.isFinite(data.sentRate) ? data.sentRate : undefined
      if (!data.active) this.session = ''
      else this.queueEffect()
      this.publish()
      if (data.error) this.error(data.error)
    }
  }

  settings(sensitivity: number, smoothing: number) {
    this.sensitivity = sensitivity
    this.smoothing = smoothing
    this.bridge?.postMessage(JSON.stringify({ action: 'settings', session: this.session, sensitivity, smoothing }))
  }

  effectSettings(settings: AudioEffectSettings) {
    this.effect = { ...settings, colors: [...settings.colors] }
    ++this.effectRevision
    this.queueEffect()
  }

  private queueEffect() {
    if (!this.state.active || !this.session || this.effectBusy || this.effectTimer !== undefined || this.effectSent === this.effectRevision) return
    this.effectTimer = window.setTimeout(() => {
      this.effectTimer = undefined
      void this.sendEffect()
    }, 60)
  }

  private async sendEffect() {
    if (!this.state.active || !this.session || this.effectBusy) return
    const session = this.session, revision = this.effectRevision
    this.effectBusy = true
    try {
      await request(`/audio/settings?session=${session}`, JSON.stringify(this.effect))
      if (session === this.session) this.effectSent = revision
    } catch (error) {
      if (session === this.session) {
        this.effectSent = revision // Do not retry every meter update on an older firmware.
        this.error(`Audio settings were not applied. Check the Audio Reactive firmware version. ${error instanceof Error ? error.message : ''}`)
      }
    } finally { this.effectBusy = false; this.queueEffect() }
  }

  async start(source: AudioSource) {
    if (this.session) return
    if (source === 'playback' && !this.state.playback) {
      this.error('Device audio is unavailable on this platform. Select Microphone instead.')
      return
    }
    if (!this.bridge && (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia)) {
      this.error('This HTTP page cannot access audio. Use the updated AuraX Finder app. Desktop capture requires a secure HTTPS page.')
      return
    }
    const session = Array.from(crypto.getRandomValues(new Uint8Array(16)), n => n.toString(16).padStart(2, '0')).join('')
    this.session = session
    this.effectSent = -1
    this.source = source
    this.state.starting = true
    this.publish()
    if (this.bridge) {
      this.bridge.postMessage(JSON.stringify({ action: 'start', session, source, sensitivity: this.sensitivity, smoothing: this.smoothing }))
      this.permissionTimer = window.setTimeout(() => {
        if (this.session === session && this.state.starting) {
          void this.stop()
          this.error('Audio permission or startup timed out. Please try again.')
        }
      }, 60000)
      return
    }

    let stream: MediaStream | undefined
    let context: AudioContext | undefined
    try {
      const Context = window.AudioContext ?? (window as AudioWindow).webkitAudioContext
      if (!Context) throw new Error('Web Audio is unavailable')
      context = new Context()
      this.context = context
      await context.resume()
      stream = source === 'playback'
        ? await navigator.mediaDevices.getDisplayMedia({ video: true, audio: true })
        : await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } })
      if (this.session !== session) { stream.getTracks().forEach(t => t.stop()); return }
      this.stream = stream
      if (!stream.getAudioTracks().length) throw new Error('No audio was shared. Select a browser tab and enable Share tab audio.')
      stream.getTracks().forEach(track => track.addEventListener('ended', () => { if (this.session === session) void this.stop() }))
      const analyser = context.createAnalyser()
      analyser.fftSize = 1024
      analyser.smoothingTimeConstant = 0
      analyser.minDecibels = -90
      analyser.maxDecibels = -10
      const input = context.createMediaStreamSource(stream)
      input.connect(analyser)
      const time = new Float32Array(analyser.fftSize)
      const frequency = new Uint8Array(analyser.frequencyBinCount)
      const sampleRate = context.sampleRate
      const band = (low: number, high: number) => {
        const first = Math.max(1, Math.ceil(low * analyser.fftSize / sampleRate))
        const last = Math.min(frequency.length - 1, Math.floor(high * analyser.fftSize / sampleRate))
        let total = 0
        for (let i = first; i <= last; i++) total += frequency[i]
        return last < first ? 0 : total / (last - first + 1) / 255
      }
      await request(`/audio/start?session=${session}`)
      if (this.session !== session) { void request(`/audio/stop?session=${session}`).catch(() => {}); return }
      this.state.starting = false
      this.state.active = true
      this.queueEffect()
      this.publish()
      let baseline = 0.08, lastBeat = 0, lastSent = 0, pendingBeat = false, busy = false, failures = 0
      let smoothed = [0, 0, 0, 0], lastAnalysis = performance.now()
      this.timer = window.setInterval(() => {
        if (this.session !== session) return
        analyser.getFloatTimeDomainData(time)
        analyser.getByteFrequencyData(frequency)
        let energy = 0
        for (const sample of time) energy += sample * sample
        const compress = (v: number, gain: number) => 1 - Math.exp(-Math.max(0, v - 0.012) * gain * 1.3)
        const volume = compress(Math.sqrt(energy / time.length), 7.5)
        const bass = compress(band(40, 250), 3.7)
        const now = performance.now()
        if (bass > Math.max(0.22, baseline * 1.42) && volume > 0.12 && now - lastBeat > 190) {
          pendingBeat = true
          lastBeat = now
        }
        baseline = baseline * 0.94 + bass * 0.06
        const raw = [volume, bass, compress(band(250, 2000), 3.1), compress(band(2000, 8000), 3.4)]
        smoothed = raw.map((value, i) => releaseEnvelope(smoothed[i], value, this.smoothing, now - lastAnalysis))
        lastAnalysis = now
        if (busy || now - lastSent < 25) return
        const gain = audioSensitivityGain(this.sensitivity)
        const level = (index: number) => audioByteLevel(smoothed[index] * gain)
        const levels = { volume: level(0), bass: level(1), mid: level(2),
          treble: level(3), beat: pendingBeat && gain > 0 ? 255 : 0 }
        pendingBeat = false
        busy = true
        lastSent = now
        this.state.levels = levels
        this.publish()
        void request(`/audio/data?session=${session}`, encodeAudioFrame(levels)).then(() => { failures = 0 }).catch(error => {
          if (++failures >= 2 && this.session === session) {
            void this.stop()
            this.error(`Audio connection stopped: ${error.message}`)
          }
        }).finally(() => { busy = false })
      }, 25)
    } catch (error) {
      stream?.getTracks().forEach(t => t.stop())
      if (context && context.state !== 'closed') void context.close().catch(() => {})
      if (this.session === session) {
        await this.stop()
        this.error(error instanceof Error ? error.message : 'Audio capture failed')
      }
    }
  }

  async stop(restore = true, beacon = false) {
    const session = this.session
    this.session = ''
    clearInterval(this.timer)
    clearTimeout(this.permissionTimer)
    clearTimeout(this.effectTimer)
    this.effectTimer = undefined
    this.stream?.getTracks().forEach(t => t.stop())
    this.stream = undefined
    if (this.context && this.context.state !== 'closed') void this.context.close().catch(() => {})
    this.context = undefined
    this.state.active = this.state.starting = false
    this.state.levels = SILENCE
    this.publish()
    if (!session) return
    this.bridge?.postMessage(JSON.stringify({ action: 'stop', session, restore }))
    const url = `/audio/stop?session=${session}&restore=${restore ? 1 : 0}`
    if (beacon && navigator.sendBeacon?.(url, '')) return
    await request(url).catch(() => {})
  }

  private onPageHide = () => { void this.stop(true, true) }
  private onVisibility = () => {
    if (document.hidden && !this.state.starting && (!this.bridge || this.source === 'microphone')) void this.stop(true, true)
  }
  dispose() {
    void this.stop(true, true)
    window.removeEventListener('aurax-audio', this.onNative)
    window.removeEventListener('pagehide', this.onPageHide)
    document.removeEventListener('visibilitychange', this.onVisibility)
    if ((window as AudioWindow).AuraXAudio) (window as AudioWindow).AuraXAudio!.onmessage = undefined
  }
}
