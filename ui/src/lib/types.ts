export interface StatusResponse {
  playing: boolean
  file?: string
  commands?: number
  frames_rendered?: number
  frames_expected?: number
  ip: string
  hostname?: string
  wifi_mode?: number
  ap_ssid?: string
  ap_mode?: boolean
  battery_pct: number
  battery_mv: number
  sync_channel?: number
  rssi?: number
}

export interface PeerInfo {
  hostname: string
  ip: string
  bat_pct: number
  rssi?: number
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
  hostname?: string
  syncChannel?: number
  brightness?: number
  effectId?: number
  effectSpeed?: number
  effectDotSize?: number
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
