import ArrowDropDownIcon from '@mui/icons-material/ArrowDropDown';
import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import ButtonGroup from '@mui/material/ButtonGroup';
import Divider from '@mui/material/Divider';
import FormControl from '@mui/material/FormControl';
import InputLabel from '@mui/material/InputLabel';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import Paper from '@mui/material/Paper';
import Select from '@mui/material/Select';
import Typography from '@mui/material/Typography';
import { useRef, useState } from 'react';
import type { DeviceState } from '../device/protocol';
import type { DeviceActions } from '../device/useDeviceSession';
import { BootBootsControls, PeripheralControls } from './BootBootsControls';
import { DeviceStatusPanel, ImageGallery, LogViewer } from './DevicePanels';

/**
 * Everything you can do with a connected catcam, whichever transport carries the commands: status, logs,
 * images, photos, reboot, settings and the peripheral test panel.
 */
export function DeviceConsole({
  state,
  actions,
  showResponseLog = false,
}: {
  state: DeviceState;
  actions: DeviceActions;
  showResponseLog?: boolean;
}) {
  const [logsMenuOpen, setLogsMenuOpen] = useState(false);
  const logsAnchor = useRef<HTMLDivElement>(null);
  const [previousLog, setPreviousLog] = useState('');
  const logsLoading = state.logs.loading;

  const reboot = () => {
    if (window.confirm('Are you sure you want to reboot the device?')) void actions.reboot();
  };
  const fromMenu = (fn: () => unknown) => () => {
    setLogsMenuOpen(false);
    void fn();
  };

  return (
    <div>
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', alignItems: 'center' }}>
        <Button variant="outlined" onClick={actions.requestStatus}>
          Refresh Status
        </Button>
        <ButtonGroup variant="outlined" ref={logsAnchor} disabled={logsLoading}>
          <Button onClick={() => actions.requestLogs(150)}>{logsLoading ? 'Loading...' : 'Get Logs'}</Button>
          <Button size="small" aria-label="More log options" onClick={() => setLogsMenuOpen(true)}>
            <ArrowDropDownIcon />
          </Button>
        </ButtonGroup>
        <Menu anchorEl={logsAnchor.current} open={logsMenuOpen} onClose={() => setLogsMenuOpen(false)}>
          <MenuItem onClick={fromMenu(() => actions.requestLogs(50))}>Last 50 entries</MenuItem>
          <MenuItem onClick={fromMenu(() => actions.requestLogs(150))}>Last 150 entries</MenuItem>
          <MenuItem onClick={fromMenu(() => actions.requestLogs())}>Entire log file</MenuItem>
          <Divider />
          <MenuItem disabled={state.previousLogs.loading} onClick={fromMenu(actions.listPreviousLogs)}>
            {state.previousLogs.loading ? 'Loading...' : 'Previous logs...'}
          </MenuItem>
        </Menu>
        {state.previousLogs.files.length > 0 && (
          <FormControl size="small" sx={{ minWidth: 220 }}>
            <InputLabel id="previous-log-label">Previous log</InputLabel>
            <Select
              labelId="previous-log-label"
              label="Previous log"
              value={previousLog}
              disabled={logsLoading}
              onChange={(e) => {
                const filename = String(e.target.value);
                setPreviousLog(filename);
                if (filename) void actions.getLogFile(filename);
              }}
            >
              <MenuItem value="">
                <em>None</em>
              </MenuItem>
              {[...state.previousLogs.files].reverse().map((f) => (
                <MenuItem key={f} value={f}>
                  {f}
                </MenuItem>
              ))}
            </Select>
          </FormControl>
        )}
        <Button variant="outlined" color="success" onClick={actions.ping}>
          Ping
        </Button>
        <Button
          variant="outlined"
          color="warning"
          disabled={state.imageList.loading}
          onClick={actions.listImages}
        >
          {state.imageList.loading
            ? state.imageList.progress
              ? `Loading ${state.imageList.progress.current}/${state.imageList.progress.total}...`
              : 'Loading...'
            : 'Get Images'}
        </Button>
        <Button variant="contained" color="error" disabled={state.takingPhoto} onClick={actions.takePhoto}>
          {state.takingPhoto ? 'Capturing...' : 'Take Photo'}
        </Button>
        <Button variant="outlined" color="error" disabled={state.rebooting} onClick={reboot}>
          {state.rebooting ? 'Rebooting...' : 'Reboot'}
        </Button>
      </div>

      {state.version && (
        <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
          Firmware {state.version}
        </Typography>
      )}

      {state.error && (
        <Alert severity="error" onClose={actions.clearError} sx={{ mt: 2 }}>
          {state.error}
        </Alert>
      )}

      <PeripheralControls state={state} actions={actions} />
      <BootBootsControls state={state} actions={actions} />
      <ImageGallery state={state} onSelect={actions.selectImage} />
      <LogViewer logData={state.logs.text} />
      {state.status && <DeviceStatusPanel status={state.status} lastUpdate={state.lastUpdate} />}

      {showResponseLog && state.responseLog.length > 0 && (
        <Paper variant="outlined" sx={{ p: 2, mt: 2 }}>
          <Typography variant="subtitle2">Response Log</Typography>
          <div style={{ maxHeight: 150, overflow: 'auto', marginTop: 8 }}>
            {state.responseLog.map((line, i) => (
              // biome-ignore lint/suspicious/noArrayIndexKey: an append-only log
              <div key={i} style={{ fontFamily: 'monospace', fontSize: 11, color: '#abb2bf' }}>
                {line}
              </div>
            ))}
          </div>
        </Paper>
      )}
    </div>
  );
}
