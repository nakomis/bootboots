import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardActionArea from '@mui/material/CardActionArea';
import Chip from '@mui/material/Chip';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import Slider from '@mui/material/Slider';
import Typography from '@mui/material/Typography';
import { useCallback, useEffect, useState } from 'react';
import { useBootboots } from '../BootbootsContext';
import { type CatcamEvent, getEvents } from '../services/events';

/** Display only: the device's own threshold is set on the Remote/Bluetooth pages. */
const TRIGGER_THRESHOLD = 0.8;
const TRIGGER_COLOUR = '#c0392b';
const NEAR_MISS_COLOUR = '#e67e22';

export const confidencePct = (v: number) => `${(v * 100).toFixed(1)}%`;
const formatTimestamp = (iso: string) =>
  new Date(iso).toLocaleString('en-GB', { dateStyle: 'short', timeStyle: 'medium' });

function ConfidenceBadge({ confidence, prefix = '' }: { confidence: number; prefix?: string }) {
  return (
    <Chip
      size="small"
      label={`${prefix}${confidencePct(confidence)}`}
      sx={{
        fontWeight: 'bold',
        color: 'white',
        bgcolor: confidence >= TRIGGER_THRESHOLD ? TRIGGER_COLOUR : NEAR_MISS_COLOUR,
      }}
    />
  );
}

/** Catcam detections and near-misses (the old "Catches" tab). */
export function CatchesPage() {
  const { aws } = useBootboots();
  const [events, setEvents] = useState<CatcamEvent[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [minConfidence, setMinConfidence] = useState(0.5);
  const [selected, setSelected] = useState<CatcamEvent | null>(null);

  const fetchEvents = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      setEvents(await getEvents(aws, minConfidence));
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load events');
    } finally {
      setLoading(false);
    }
  }, [aws, minConfidence]);

  useEffect(() => {
    void fetchEvents();
  }, [fetchEvents]);

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 16, marginBottom: 16, flexWrap: 'wrap' }}>
        <Typography variant="h5">Catches</Typography>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12, minWidth: 260 }}>
          <Typography variant="body2" noWrap>
            Min confidence: <strong>{confidencePct(minConfidence)}</strong>
          </Typography>
          <Slider
            size="small"
            min={0.5}
            max={1}
            step={0.05}
            value={minConfidence}
            onChange={(_, v) => setMinConfidence(v as number)}
            aria-label="Minimum confidence"
            sx={{ width: 120 }}
          />
        </div>
        <Button variant="contained" size="small" onClick={fetchEvents} disabled={loading}>
          {loading ? 'Loading…' : 'Refresh'}
        </Button>
        <Typography variant="body2" color="text.secondary">
          {events.length} event{events.length !== 1 ? 's' : ''} ·{' '}
          <span style={{ color: TRIGGER_COLOUR }}>■</span> ≥{confidencePct(TRIGGER_THRESHOLD)} trigger ·{' '}
          <span style={{ color: NEAR_MISS_COLOUR }}>■</span> near-miss
        </Typography>
      </div>

      {error && (
        <Alert severity="error" sx={{ mb: 2 }}>
          {error}
        </Alert>
      )}
      {!loading && !error && events.length === 0 && (
        <Typography color="text.secondary">
          No events found above {confidencePct(minConfidence)} confidence.
        </Typography>
      )}

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(200px, 1fr))', gap: 12 }}>
        {events.map((event) => (
          <Card key={event.id} variant="outlined">
            <CardActionArea onClick={() => setSelected(event)}>
              {event.imageUrl ? (
                <img
                  src={event.imageUrl}
                  alt="catcam frame"
                  loading="lazy"
                  style={{ width: '100%', aspectRatio: '4/3', objectFit: 'cover', display: 'block' }}
                />
              ) : (
                <div
                  style={{
                    width: '100%',
                    aspectRatio: '4/3',
                    background: '#111',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    color: '#555',
                  }}
                >
                  No image
                </div>
              )}
              <div style={{ padding: '6px 8px' }}>
                <ConfidenceBadge confidence={event.bootsConfidence} />
                <Typography variant="caption" color="text.secondary" component="div" sx={{ mt: 0.5 }}>
                  {formatTimestamp(event.timestamp)}
                </Typography>
              </div>
            </CardActionArea>
          </Card>
        ))}
      </div>

      <Dialog open={!!selected} onClose={() => setSelected(null)} maxWidth="md" fullWidth>
        {selected && (
          <>
            {selected.imageUrl && (
              <img
                src={selected.imageUrl}
                alt="catcam frame"
                style={{ width: '100%', maxHeight: '60vh', objectFit: 'contain', display: 'block' }}
              />
            )}
            <DialogContent>
              <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 8 }}>
                <ConfidenceBadge confidence={selected.bootsConfidence} prefix="Boots " />
                {selected.bootsConfidence >= TRIGGER_THRESHOLD ? (
                  <Typography variant="body2" sx={{ color: TRIGGER_COLOUR }}>
                    TRIGGERED deterrent
                  </Typography>
                ) : (
                  <Typography variant="body2" sx={{ color: NEAR_MISS_COLOUR }}>
                    Near-miss (below trigger threshold)
                  </Typography>
                )}
              </div>
              <Typography variant="body2">
                <strong>Time:</strong> {formatTimestamp(selected.timestamp)}
              </Typography>
              <Typography variant="body2" sx={{ wordBreak: 'break-all' }}>
                <strong>File:</strong> {selected.imageName}
              </Typography>
              {selected.claudeResult && (
                <Typography variant="body2" sx={{ mt: 1 }} title={selected.claudeResult.reasoning}>
                  <strong>Claude:</strong> {selected.claudeResult.cat}{' '}
                  <span style={{ color: '#abb2bf' }}>({selected.claudeResult.confidence} confidence)</span>
                </Typography>
              )}
            </DialogContent>
            <DialogActions>
              <Button onClick={() => setSelected(null)}>Close</Button>
            </DialogActions>
          </>
        )}
      </Dialog>
    </div>
  );
}
