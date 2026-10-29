import MenuItem from '@mui/material/MenuItem';
import Select from '@mui/material/Select';
import Slider from '@mui/material/Slider';
import Switch from '@mui/material/Switch';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { type ReactNode, useEffect, useState } from 'react';
import {
  type CameraSettings,
  FRAME_SIZE_OPTIONS,
  SPECIAL_EFFECT_NAMES,
  WB_MODE_NAMES,
} from '../device/types';
import { Section } from './Section';

type OnChange = (setting: string, value: number | boolean) => void;

const rowStyle = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  gap: 12,
  marginBottom: 4,
};

/** Slider plus a text box; the text box commits on blur or Enter, clamped to the range. */
export function CameraSlider({
  label,
  value,
  min,
  max,
  setting,
  onChange,
  disabled,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  setting: string;
  onChange: OnChange;
  disabled: boolean;
}) {
  const [text, setText] = useState(String(value));
  const [sliding, setSliding] = useState(value);
  useEffect(() => {
    setText(String(value));
    setSliding(value);
  }, [value]);

  const commitText = () => {
    const parsed = Number.parseInt(text, 10);
    if (Number.isNaN(parsed)) {
      setText(String(value));
      return;
    }
    const clamped = Math.max(min, Math.min(max, parsed));
    setText(String(clamped));
    if (clamped !== value) onChange(setting, clamped);
  };

  return (
    <div style={rowStyle}>
      <Typography variant="body2" sx={{ minWidth: 130 }}>
        {label}
      </Typography>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, flex: 1, maxWidth: 280 }}>
        <Slider
          size="small"
          min={min}
          max={max}
          value={sliding}
          disabled={disabled}
          onChange={(_, v) => setSliding(v as number)}
          onChangeCommitted={(_, v) => v !== value && onChange(setting, v as number)}
          aria-label={label}
        />
        <TextField
          size="small"
          value={text}
          disabled={disabled}
          onChange={(e) => setText(e.target.value)}
          onBlur={commitText}
          onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
          slotProps={{
            htmlInput: { 'aria-label': `${label} value`, style: { width: 44, textAlign: 'right' } },
          }}
        />
      </div>
    </div>
  );
}

export function CameraToggle({
  label,
  value,
  setting,
  onChange,
  disabled,
}: {
  label: string;
  value: boolean;
  setting: string;
  onChange: OnChange;
  disabled: boolean;
}) {
  return (
    <div style={rowStyle}>
      <Typography variant="body2">{label}</Typography>
      <Switch
        size="small"
        checked={value}
        disabled={disabled}
        onChange={() => onChange(setting, !value)}
        slotProps={{ input: { 'aria-label': label } }}
      />
    </div>
  );
}

export function CameraSelect({
  label,
  value,
  options,
  setting,
  onChange,
  disabled,
}: {
  label: string;
  value: number;
  options: { value: number; label: string }[];
  setting: string;
  onChange: OnChange;
  disabled: boolean;
}) {
  return (
    <div style={rowStyle}>
      <Typography variant="body2">{label}</Typography>
      <Select
        size="small"
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(setting, Number(e.target.value))}
        inputProps={{ 'aria-label': label }}
      >
        {options.map((o) => (
          <MenuItem key={o.value} value={o.value}>
            {o.label}
          </MenuItem>
        ))}
      </Select>
    </div>
  );
}

const named = (names: string[]) => names.map((label, value) => ({ value, label }));

function Group({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div style={{ borderBottom: '1px solid rgba(255,255,255,0.12)', paddingBottom: 12 }}>
      <Typography variant="overline" color="text.secondary">
        {title}
      </Typography>
      {children}
    </div>
  );
}

export function CameraSettingsPanel({
  settings: s,
  onSettingChange: on,
  disabled: d,
}: {
  settings: CameraSettings;
  onSettingChange: OnChange;
  disabled: boolean;
}) {
  return (
    <Section title="Camera Settings">
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12, maxWidth: 520 }}>
        <Group title="Resolution & quality">
          <CameraSelect
            label="Frame Size"
            value={s.frame_size}
            options={FRAME_SIZE_OPTIONS}
            setting="frame_size"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="JPEG Quality"
            value={s.jpeg_quality}
            min={0}
            max={63}
            setting="jpeg_quality"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="Frame Buffers"
            value={s.fb_count}
            min={1}
            max={3}
            setting="fb_count"
            onChange={on}
            disabled={d}
          />
          <Typography variant="caption" color="text.secondary">
            JPEG Quality: lower = better quality. Frame Buffers: changes take effect on reboot.
          </Typography>
        </Group>
        <Group title="Image quality">
          <CameraSlider
            label="Brightness"
            value={s.brightness}
            min={-2}
            max={2}
            setting="brightness"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="Contrast"
            value={s.contrast}
            min={-2}
            max={2}
            setting="contrast"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="Saturation"
            value={s.saturation}
            min={-2}
            max={2}
            setting="saturation"
            onChange={on}
            disabled={d}
          />
          <CameraSelect
            label="Special Effect"
            value={s.special_effect}
            options={named(SPECIAL_EFFECT_NAMES)}
            setting="special_effect"
            onChange={on}
            disabled={d}
          />
        </Group>
        <Group title="White balance">
          <CameraToggle
            label="White Balance"
            value={s.white_balance}
            setting="white_balance"
            onChange={on}
            disabled={d}
          />
          <CameraToggle label="AWB Gain" value={s.awb_gain} setting="awb_gain" onChange={on} disabled={d} />
          <CameraSelect
            label="WB Mode"
            value={s.wb_mode}
            options={named(WB_MODE_NAMES)}
            setting="wb_mode"
            onChange={on}
            disabled={d}
          />
        </Group>
        <Group title="Exposure">
          <CameraToggle
            label="Auto Exposure"
            value={s.exposure_ctrl}
            setting="exposure_ctrl"
            onChange={on}
            disabled={d}
          />
          <CameraToggle label="AEC DSP" value={s.aec2} setting="aec2" onChange={on} disabled={d} />
          <CameraSlider
            label="AE Level"
            value={s.ae_level}
            min={-2}
            max={2}
            setting="ae_level"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="AEC Value"
            value={s.aec_value}
            min={0}
            max={1200}
            setting="aec_value"
            onChange={on}
            disabled={d}
          />
        </Group>
        <Group title="Gain">
          <CameraToggle
            label="Auto Gain"
            value={s.gain_ctrl}
            setting="gain_ctrl"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="AGC Gain"
            value={s.agc_gain}
            min={0}
            max={30}
            setting="agc_gain"
            onChange={on}
            disabled={d}
          />
          <CameraSlider
            label="Gain Ceiling"
            value={s.gain_ceiling}
            min={0}
            max={6}
            setting="gain_ceiling"
            onChange={on}
            disabled={d}
          />
        </Group>
        <Group title="Corrections">
          <CameraToggle label="Bad Pixel Correction" value={s.bpc} setting="bpc" onChange={on} disabled={d} />
          <CameraToggle
            label="White Pixel Correction"
            value={s.wpc}
            setting="wpc"
            onChange={on}
            disabled={d}
          />
          <CameraToggle
            label="Gamma Correction"
            value={s.raw_gma}
            setting="raw_gma"
            onChange={on}
            disabled={d}
          />
          <CameraToggle label="Lens Correction" value={s.lenc} setting="lenc" onChange={on} disabled={d} />
          <CameraToggle label="Downsize Enable" value={s.dcw} setting="dcw" onChange={on} disabled={d} />
        </Group>
        <Group title="Orientation">
          <CameraToggle
            label="Horizontal Mirror"
            value={s.hmirror}
            setting="hmirror"
            onChange={on}
            disabled={d}
          />
          <CameraToggle label="Vertical Flip" value={s.vflip} setting="vflip" onChange={on} disabled={d} />
        </Group>
        <Group title="Test">
          <CameraToggle label="Colour Bar" value={s.colorbar} setting="colorbar" onChange={on} disabled={d} />
        </Group>
        <div>
          <Typography variant="overline" color="text.secondary">
            Flash
          </Typography>
          <CameraSlider
            label="LED Delay (millis)"
            value={s.led_delay_millis}
            min={0}
            max={1000}
            setting="led_delay_millis"
            onChange={on}
            disabled={d}
          />
        </div>
      </div>
    </Section>
  );
}
