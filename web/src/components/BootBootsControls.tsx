import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Slider from '@mui/material/Slider';
import Switch from '@mui/material/Switch';
import Typography from '@mui/material/Typography';
import { useEffect, useState } from 'react';
import type { DeviceState } from '../device/protocol';
import type { DeviceActions } from '../device/useDeviceSession';
import { CameraSettingsPanel } from './CameraSettingsPanel';
import { Section, SettingRow } from './Section';

/** Training mode, dry run, trigger threshold, Claude vision and camera settings. */
export function BootBootsControls({ state, actions }: { state: DeviceState; actions: DeviceActions }) {
  const { settings, updatingSetting: busy } = state;
  // The slider commits to the device on release.
  const [thresh, setThresh] = useState(settings.triggerThresh);
  useEffect(() => setThresh(settings.triggerThresh), [settings.triggerThresh]);

  return (
    <Section title="BootBoots Settings">
      <SettingRow
        label="Training Mode"
        help="Captures photos without AI inference or deterrent activation. Photos are uploaded to S3 training/ prefix for model training."
      >
        <Switch
          checked={settings.trainingMode}
          disabled={busy}
          onChange={actions.toggleTrainingMode}
          slotProps={{ input: { 'aria-label': 'Training Mode' } }}
        />
      </SettingRow>
      {settings.trainingMode && (
        <Alert severity="warning" sx={{ mb: 1 }}>
          Training mode is active. Motion-triggered photos will be captured without inference.
        </Alert>
      )}
      <SettingRow
        label="Dry Run"
        help="Runs the full deterrent sequence (LEDs, video, notifications) but skips the water mist."
      >
        <Switch
          checked={settings.dryRun}
          disabled={busy}
          onChange={actions.toggleDryRun}
          slotProps={{ input: { 'aria-label': 'Dry Run' } }}
        />
      </SettingRow>
      <SettingRow
        label="Trigger Threshold"
        help="Minimum Boots confidence required to activate the deterrent."
      >
        <Typography fontWeight="bold">{Math.round(thresh * 100)}%</Typography>
      </SettingRow>
      <Slider
        min={0.5}
        max={1}
        step={0.05}
        marks={[
          { value: 0.5, label: '50%' },
          { value: 1, label: '100%' },
        ]}
        value={thresh}
        disabled={busy}
        onChange={(_, v) => setThresh(v as number)}
        onChangeCommitted={(_, v) => actions.setTriggerThresh(v as number)}
        aria-label="Trigger Threshold"
        sx={{ mb: 2 }}
      />
      <SettingRow
        label="Claude Vision"
        help="Sends each image to Claude for cat identification. Results are informational only and do not affect the deterrent trigger."
      >
        <Switch
          checked={settings.claudeInfer}
          disabled={busy}
          onChange={actions.toggleClaudeInfer}
          slotProps={{ input: { 'aria-label': 'Claude Vision' } }}
        />
      </SettingRow>
      <CameraSettingsPanel
        settings={state.camera}
        onSettingChange={actions.setCameraSetting}
        disabled={busy}
      />
    </Section>
  );
}

/** Hardware test panel: PIR indicator, PCF8574 outputs and a simulated detection. */
export function PeripheralControls({ state, actions }: { state: DeviceState; actions: DeviceActions }) {
  const { peripherals: p, peripheralPending: pending, status } = state;
  const notReady = !!status && status.system.pcf8574_ready === false;
  const toggle = (label: string, pin: string, peripheral: string, on: boolean) => (
    <SettingRow
      label={
        <>
          {label}{' '}
          <Typography component="span" variant="caption" color="text.secondary">
            {pin}
          </Typography>
        </>
      }
    >
      <Switch
        checked={on}
        color={peripheral === 'spray' ? 'error' : 'primary'}
        disabled={notReady || pending === peripheral}
        onChange={() => actions.setPeripheral(peripheral, !on)}
        slotProps={{ input: { 'aria-label': label } }}
      />
    </SettingRow>
  );

  return (
    <Section title="Peripheral Controls" defaultExpanded>
      {notReady && (
        <Alert severity="warning" sx={{ mb: 1 }}>
          PCF8574 not ready — check I2C connections
        </Alert>
      )}
      <SettingRow
        label={
          <>
            PIR Motion Sensor{' '}
            <Typography component="span" variant="caption" color="text.secondary">
              GPIO 42 · read-only
            </Typography>
          </>
        }
      >
        <span
          title={p.pir_active ? 'Motion detected' : 'No motion'}
          style={{
            display: 'inline-block',
            width: 14,
            height: 14,
            borderRadius: '50%',
            backgroundColor: p.pir_active ? '#98c379' : '#5c6370',
            boxShadow: p.pir_active ? '0 0 6px #98c379' : 'none',
          }}
        />
      </SettingRow>
      {toggle('Flash LED', 'P7', 'flash_led', p.flash_led_on)}
      {toggle('LED Strip', 'P5+P6', 'led_strip', p.led_strip_on)}
      {toggle('Spray (Atomiser)', 'P3', 'spray', p.spray_on)}
      <SettingRow label="Simulate Boots Detection" help="no photo · no SageMaker">
        <Button
          variant="contained"
          color="warning"
          disabled={state.simulatePending}
          onClick={actions.simulateDetection}
        >
          {state.simulatePending ? 'Running (~10s)…' : 'Trigger Deterrent Sequence'}
        </Button>
      </SettingRow>
    </Section>
  );
}
