// The catcam's command/response protocol. One CommandDispatcher in the firmware serves both BLE and MQTT,
// so the same commands and responses apply to either transport.

/** Camera sensor settings synced with the device (set via set_setting "camera_<name>"). */
export interface CameraSettings {
  frame_size: number;
  jpeg_quality: number;
  fb_count: number;
  brightness: number;
  contrast: number;
  saturation: number;
  special_effect: number;
  white_balance: boolean;
  awb_gain: boolean;
  wb_mode: number;
  exposure_ctrl: boolean;
  aec2: boolean;
  ae_level: number;
  aec_value: number;
  gain_ctrl: boolean;
  agc_gain: number;
  gain_ceiling: number;
  bpc: boolean;
  wpc: boolean;
  raw_gma: boolean;
  lenc: boolean;
  hmirror: boolean;
  vflip: boolean;
  dcw: boolean;
  colorbar: boolean;
  led_delay_millis: number;
}

export const DEFAULT_CAMERA_SETTINGS: CameraSettings = {
  frame_size: 13,
  jpeg_quality: 10,
  fb_count: 2,
  brightness: 0,
  contrast: 0,
  saturation: 0,
  special_effect: 0,
  white_balance: true,
  awb_gain: true,
  wb_mode: 0,
  exposure_ctrl: true,
  aec2: false,
  ae_level: -2,
  aec_value: 300,
  gain_ctrl: true,
  agc_gain: 15,
  gain_ceiling: 0,
  bpc: false,
  wpc: true,
  raw_gma: true,
  lenc: true,
  hmirror: false,
  vflip: false,
  dcw: true,
  colorbar: false,
  led_delay_millis: 100,
};

export interface PeripheralState {
  pir_active: boolean;
  flash_led_on: boolean;
  led_strip_on: boolean;
  spray_on: boolean;
}

export interface BootBootsSystemStatus {
  device: string;
  timestamp: number;
  uptime_seconds: number;
  system: {
    initialized: boolean;
    camera_ready: boolean;
    wifi_connected: boolean;
    sd_card_ready: boolean;
    i2c_ready: boolean;
    pcf8574_ready?: boolean;
    atomizer_enabled: boolean;
    training_mode: boolean;
  };
  statistics: {
    total_detections: number;
    boots_detections: number;
    atomizer_activations: number;
    false_positives_avoided: number;
  };
  timing: {
    last_detection: number;
    last_status_report: number;
  };
  peripherals?: PeripheralState;
}

export interface DeviceCommand {
  command: string;
  [key: string]: unknown;
}

export interface DeviceResponse {
  type?: string;
  [key: string]: unknown;
}

/** Something that can send commands to the catcam: Web Bluetooth or Fleet (MQTT). */
export interface DeviceTransport {
  send(command: DeviceCommand): Promise<void>;
  /** BLE can read the status characteristic directly; otherwise get_status is sent. */
  readStatus?(): Promise<BootBootsSystemStatus | null>;
}

export const isSystemStatus = (value: unknown): value is BootBootsSystemStatus =>
  !!value && typeof value === 'object' && 'system' in value && 'statistics' in value;

export const CAT_NAMES = ['Boots', 'Chi', 'Kappa', 'Mu', 'Tau', 'NoCat'];
export const SPECIAL_EFFECT_NAMES = [
  'None',
  'Negative',
  'Grayscale',
  'Red Tint',
  'Green Tint',
  'Blue Tint',
  'Sepia',
];
export const WB_MODE_NAMES = ['Auto', 'Sunny', 'Cloudy', 'Office', 'Home'];
export const FRAME_SIZE_OPTIONS: { value: number; label: string }[] = [
  { value: 5, label: 'QVGA (320x240)' },
  { value: 6, label: 'CIF (400x296)' },
  { value: 8, label: 'VGA (640x480)' },
  { value: 9, label: 'SVGA (800x600)' },
  { value: 10, label: 'XGA (1024x768)' },
  { value: 11, label: 'HD (1280x720)' },
  { value: 12, label: 'SXGA (1280x1024)' },
  { value: 13, label: 'UXGA (1600x1200)' },
];
