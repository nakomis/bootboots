import FormControl from '@mui/material/FormControl';
import InputLabel from '@mui/material/InputLabel';
import LinearProgress from '@mui/material/LinearProgress';
import MenuItem from '@mui/material/MenuItem';
import Paper from '@mui/material/Paper';
import Select from '@mui/material/Select';
import Typography from '@mui/material/Typography';
import type { DeviceState } from '../device/protocol';
import { type BootBootsSystemStatus, CAT_NAMES } from '../device/types';

export const formatUptime = (seconds: number) =>
  `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m ${seconds % 60}s`;

const tick = (ok: boolean) => (ok ? '✅' : '❌');

export function DeviceStatusPanel({
  status,
  lastUpdate,
}: {
  status: BootBootsSystemStatus;
  lastUpdate: number | null;
}) {
  return (
    <Paper variant="outlined" sx={{ p: 2, mt: 2 }}>
      <Typography variant="h6">System Status</Typography>
      {lastUpdate && (
        <Typography variant="caption" color="text.secondary">
          Last updated: {new Date(lastUpdate).toLocaleTimeString('en-GB')}
        </Typography>
      )}
      <div
        style={{
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fit, minmax(240px, 1fr))',
          gap: 16,
          marginTop: 8,
        }}
      >
        <div>
          <Typography variant="subtitle2">System Information</Typography>
          <Typography variant="body2">Device: {status.device}</Typography>
          <Typography variant="body2">Uptime: {formatUptime(status.uptime_seconds)}</Typography>
          <Typography variant="body2">Camera Ready: {tick(status.system.camera_ready)}</Typography>
          <Typography variant="body2">WiFi Connected: {tick(status.system.wifi_connected)}</Typography>
          <Typography variant="body2">SD Card Ready: {tick(status.system.sd_card_ready)}</Typography>
          <Typography variant="body2">I2C Ready: {tick(status.system.i2c_ready)}</Typography>
          <Typography variant="body2">Atomiser Enabled: {tick(status.system.atomizer_enabled)}</Typography>
          <Typography variant="body2">
            Training Mode: {status.system.training_mode ? '🎯 ON' : '❌ OFF'}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2">Detection Statistics</Typography>
          <Typography variant="body2">Total Detections: {status.statistics.total_detections}</Typography>
          <Typography variant="body2">Boots Detections: {status.statistics.boots_detections}</Typography>
          <Typography variant="body2">
            Atomiser Activations: {status.statistics.atomizer_activations}
          </Typography>
          <Typography variant="body2">
            False Positives Avoided: {status.statistics.false_positives_avoided}
          </Typography>
        </div>
      </div>
    </Paper>
  );
}

/** Log text, either newline-joined chunks or a legacy JSON array of lines. */
export const formatLogs = (text: string) => {
  try {
    const parsed = JSON.parse(text);
    return Array.isArray(parsed) ? parsed.join('\n') : text;
  } catch {
    return text;
  }
};

export function LogViewer({ logData }: { logData: string }) {
  if (!logData) return null;
  return (
    <Paper variant="outlined" sx={{ p: 2, mt: 2 }}>
      <Typography variant="h6">Recent Log Entries</Typography>
      <pre
        style={{
          background: '#1b1e23',
          padding: 10,
          borderRadius: 5,
          overflow: 'auto',
          maxHeight: 300,
          whiteSpace: 'pre-wrap',
          fontSize: 12,
          margin: 0,
        }}
      >
        {formatLogs(logData)}
      </pre>
    </Paper>
  );
}

/** The device's local inference result for an image (the .txt next to it on the SD card). */
export function InferenceResult({ metadata, loading }: { metadata: string | null; loading: boolean }) {
  if (!metadata) {
    return (
      <Typography color="text.secondary" fontStyle="italic">
        {loading ? 'Loading...' : 'No metadata available'}
      </Typography>
    );
  }
  let data: {
    mostLikelyCat?: { name: string; confidence: number; index?: number };
    data?: { probabilities?: number[] };
  };
  try {
    data = JSON.parse(metadata);
  } catch {
    return (
      <pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word', margin: 0, fontSize: 12 }}>
        {metadata}
      </pre>
    );
  }
  return (
    <div>
      {data.mostLikelyCat && (
        <Paper sx={{ p: 1, mb: 2, bgcolor: '#1b1e23' }}>
          <Typography color="success.main" fontWeight="bold" fontSize={18}>
            {data.mostLikelyCat.name}
          </Typography>
          <Typography variant="body2" color="text.secondary">
            Confidence: {(data.mostLikelyCat.confidence * 100).toFixed(1)}%
          </Typography>
        </Paper>
      )}
      {data.data?.probabilities && (
        <div>
          <Typography variant="caption" color="text.secondary">
            All Probabilities:
          </Typography>
          {data.data.probabilities.map((prob, i) => (
            <div
              key={CAT_NAMES[i] ?? i}
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                padding: '4px 8px',
                borderRadius: 4,
                backgroundColor: i === data.mostLikelyCat?.index ? '#2c313a' : 'transparent',
              }}
            >
              <Typography variant="body2">{CAT_NAMES[i] ?? `#${i}`}</Typography>
              <Typography variant="body2" color="text.secondary">
                {(prob * 100).toFixed(1)}%
              </Typography>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function ImageGallery({
  state,
  onSelect,
}: {
  state: DeviceState;
  onSelect: (filename: string) => void;
}) {
  const { imageList, image } = state;
  if (imageList.list.length === 0) return null;
  return (
    <Paper variant="outlined" sx={{ p: 2, mt: 2 }}>
      <Typography variant="h6" gutterBottom>
        Device Images
      </Typography>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 12, flexWrap: 'wrap' }}>
        <FormControl size="small" sx={{ minWidth: 300 }}>
          <InputLabel id="image-select-label">Select image</InputLabel>
          <Select
            labelId="image-select-label"
            label="Select image"
            value={image.selected}
            disabled={image.loading}
            onChange={(e) => onSelect(String(e.target.value))}
          >
            <MenuItem value="">
              <em>None</em>
            </MenuItem>
            {[...imageList.list].reverse().map((img) => (
              <MenuItem key={img} value={img}>
                {img}
              </MenuItem>
            ))}
          </Select>
        </FormControl>
        <Typography variant="body2" color="text.secondary">
          ({imageList.list.length} images available)
        </Typography>
      </div>

      {image.loading && image.progress && (
        <div style={{ marginBottom: 12 }}>
          <Typography variant="body2">
            Loading image... ({image.progress.current}/{image.progress.total} chunks)
          </Typography>
          <LinearProgress
            variant="determinate"
            value={image.progress.total > 0 ? (image.progress.current / image.progress.total) * 100 : 0}
          />
        </div>
      )}

      {image.current && (
        <div style={{ display: 'flex', gap: 20, alignItems: 'flex-start', flexWrap: 'wrap' }}>
          <Paper variant="outlined" sx={{ p: 1, flex: '1 1 320px' }}>
            <img
              src={image.current}
              alt={image.selected}
              style={{
                maxWidth: '100%',
                maxHeight: 500,
                display: 'block',
                margin: '0 auto',
                borderRadius: 4,
              }}
            />
            <Typography variant="body2" align="center" sx={{ mt: 1 }}>
              {image.selected}
            </Typography>
          </Paper>
          <Paper variant="outlined" sx={{ p: 2, flex: '0 0 300px' }}>
            <Typography variant="subtitle1" gutterBottom>
              AI Inference Result
            </Typography>
            <InferenceResult metadata={image.metadata} loading={image.loading} />
          </Paper>
        </div>
      )}
    </Paper>
  );
}
