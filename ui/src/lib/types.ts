export interface StatusResponse {
  playing: boolean
  effect_running?: boolean
  audio_reactive?: boolean
  power_on?: boolean
  file?: string
  commands?: number
  frames_rendered?: number
  frames_expected?: number
  fps_x10?: number
  audio_packets_x10?: number
  audio_group_role?: 'sender' | 'receiver' | 'none'
  audio_group_tx_frames?: number
  audio_group_channel?: number
  ip: string
  hostname?: string
  device_name?: string
  ap_ssid?: string
  ap_mode?: boolean
  battery_pct: number
  battery_mv: number
  sync_enabled?: boolean
  sync_mask?: number
  sync_channel?: number
  rssi?: number
  fw_version?: string
  fw_build?: number
  led_count?: number
  logical_led_count?: number
  contact_poi?: boolean
  storage_mounted?: boolean
  storage_type?: string
  external_storage_detected?: boolean
  external_storage?: boolean
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
  spiFrequencyMhz?: number
  externalStorageEnabled?: boolean
  storageSckPin?: number
  storageMosiPin?: number
  storageMisoPin?: number
  storageCsPin?: number
  storageSpiFrequencyMhz?: number
  pixFile?: string
  ssid?: string
  password?: string
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
  contactPoi?: boolean
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
  fwVersion?: string
  fwBuild?: number
  updateManifestUrl?: string
  releasesUrl?: string
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
  storage_mounted?: boolean
  storage_type?: string
  external_storage_detected?: boolean
  external_storage?: boolean
  files: ProgramFile[]
}

export interface FirmwareStatus {
  current_version: string
  current_build: number
  manifest_url: string
  releases_url: string
  checked: boolean
  update_available: boolean
  checked_at_ms: number
  remote_build: number
  remote_size: number
  remote_version: string
  remote_url: string
  remote_page: string
  remote_notes: string
  error: string
}
