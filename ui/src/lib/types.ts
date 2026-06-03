export interface StatusResponse {
  playing: boolean
  effect_running?: boolean
  power_on?: boolean
  file?: string
  commands?: number
  frames_rendered?: number
  frames_expected?: number
  fps_x10?: number
  ip: string
  hostname?: string
  device_name?: string
  wifi_mode?: number
  ap_ssid?: string
  ap_mode?: boolean
  battery_pct: number
  battery_mv: number
  sync_enabled?: boolean
  sync_mask?: number
  sync_channel?: number
  rssi?: number
}

export interface PeerInfo {
  hostname: string
  ip: string
  bat_pct: number
  rssi?: number
  sync_enabled?: boolean
  sync_mask?: number
  sync_channel?: number
}

export interface Color {
  r: number
  g: number
  b: number
}

export interface Config {
  ledType?: number
  numLeds?: number
  dataPin?: number
  clkPin?: number
  pixFile?: string
  ssid?: string
  password?: string
  wifiMode?: number
  groupSsid?: string
  groupPassword?: string
  deviceName?: string
  hostname?: string
  syncChannel?: number
  syncEnabled?: boolean
  syncMask?: number
  brightness?: number
  effectId?: number
  effectSpeed?: number
  effectIntensity?: number
  effectDotSize?: number
  effectPaletteId?: number
  effectReverse?: boolean
  renderMirror?: boolean
  mALimit?: number
  batPin?: number
  batMultiplier?: number
  batCalibration?: number
  batMinMv?: number
  batMaxMv?: number
  batIntervalMs?: number
  batAutoOff?: boolean
  batAutoOffThreshold?: number
  paletteSize?: number
  paletteR?: number[]
  paletteG?: number[]
  paletteB?: number[]
}

export interface ProgramFile {
  slot: number
  name: string
  display_name?: string
  size: number
}

export interface ProgramsResponse {
  total: number
  used: number
  free: number
  selected: string
  selected_slot?: number
  files: ProgramFile[]
}
