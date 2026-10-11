import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Paper from '@mui/material/Paper';
import Typography from '@mui/material/Typography';
import { type ReactNode, useCallback, useEffect, useRef, useState } from 'react';
import { useBootboots } from '../BootbootsContext';
import bootsImg from '../images/boots.png';
import chiImg from '../images/chi.png';
import kappaImg from '../images/kappa.png';
import muImg from '../images/mu.png';
import tauImg from '../images/tau.png';
import wolfImg from '../images/wolf.jpeg';
import { claimRecord, getUnlabelledRecords, labelRecord } from '../services/catadata';
import { getTrainingImage } from '../services/images';
import type { CatadataRecord } from '../services/types';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/** "30 Mar 26 - 10:57:53" from an image name, in either filename format; null if it has no date. */
export function parseImageDate(imageName: string): string | null {
  const filename = imageName.split('/').pop() ?? '';
  let date: Date | null = null;
  // New format: 2026-03-30T09:57:53.552Z.jpg
  const iso = filename.match(/^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z)/);
  if (iso) date = new Date(iso[1]);
  else {
    // Old format: 2025-07-29T21-20-54-225Z.jpg
    const old = filename.match(/^(\d{4}-\d{2}-\d{2})T(\d{2})-(\d{2})-(\d{2})/);
    if (old) date = new Date(`${old[1]}T${old[2]}:${old[3]}:${old[4]}Z`);
  }
  if (!date || Number.isNaN(date.getTime())) return null;
  const p = (n: number) => String(n).padStart(2, '0');
  return `${p(date.getDate())} ${MONTHS[date.getMonth()]} ${String(date.getFullYear()).slice(2)} - ${p(date.getHours())}:${p(date.getMinutes())}:${p(date.getSeconds())}`;
}

const TrashIcon = (
  <svg
    xmlns="http://www.w3.org/2000/svg"
    width="60"
    height="60"
    fill="currentColor"
    viewBox="0 0 16 16"
    aria-hidden
  >
    <path d="M2.5 1a1 1 0 0 0-1 1v1a1 1 0 0 0 1 1H3v9a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2V4h.5a1 1 0 0 0 1-1V2a1 1 0 0 0-1-1H10a1 1 0 0 0-1-1H7a1 1 0 0 0-1 1zm3 4a.5.5 0 0 1 .5.5v7a.5.5 0 0 1-1 0v-7a.5.5 0 0 1 .5-.5M8 5a.5.5 0 0 1 .5.5v7a.5.5 0 0 1-1 0v-7A.5.5 0 0 1 8 5m3 .5v7a.5.5 0 0 1-1 0v-7a.5.5 0 0 1 1 0" />
  </svg>
);

const MultiIcon = (
  <svg
    xmlns="http://www.w3.org/2000/svg"
    viewBox="0 0 80 80"
    width="60"
    height="60"
    fill="currentColor"
    aria-hidden
  >
    <g opacity="0.5">
      <circle cx="50" cy="52" r="19" />
      <polygon points="35,35 41,16 48,35" />
      <polygon points="52,35 59,16 65,35" />
    </g>
    <circle cx="30" cy="52" r="19" />
    <polygon points="15,35 21,16 28,35" />
    <polygon points="32,35 39,16 45,35" />
  </svg>
);

const img = (src: string, alt: string) => (
  <img src={src} alt={alt} style={{ width: '100%', height: '100%' }} />
);

/** Label buttons with their keyboard shortcuts. Wolf is the stuffed toy used to test the sprayer. */
export const LABELS: { cat: string; key: string; title: string; content: ReactNode }[] = [
  { cat: 'Mu', key: 'm', title: 'Mu (M)', content: img(muImg, 'Mu') },
  { cat: 'Tau', key: 't', title: 'Tau (T)', content: img(tauImg, 'Tau') },
  { cat: 'Chi', key: 'c', title: 'Chi (C)', content: img(chiImg, 'Chi') },
  { cat: 'Kappa', key: 'k', title: 'Kappa (K)', content: img(kappaImg, 'Kappa') },
  { cat: 'Boots', key: 'b', title: 'Boots (B)', content: img(bootsImg, 'Boots') },
  { cat: 'Wolf', key: 'w', title: 'Wolf (W)', content: img(wolfImg, 'Wolf') },
  { cat: 'NoCat', key: 'n', title: 'No Cat (N)', content: TrashIcon },
  { cat: 'Multi', key: '2', title: 'Multiple Cats (2)', content: MultiIcon },
];

type Prediction = { prediction: string; confidence: number };

const badgeColour = (p: Prediction) =>
  p.prediction === 'Boots' && p.confidence >= 0.85
    ? '#22c55e' // confident Boots: the system would spray
    : p.confidence < 0.85
      ? '#f59e0b' // uncertain: the system abstains
      : '#1e3a8a'; // confidently not Boots

/** Cat labelling ("Giving Boots the boot"): claim an unlabelled training image and say which cat it is. */
export function LabellingPage() {
  const { aws, config, username } = useBootboots();
  const [remaining, setRemaining] = useState<number | null>(null);
  const queueRef = useRef<CatadataRecord[]>([]);
  const [current, setCurrent] = useState<CatadataRecord | null>(null);
  const [picture, setPicture] = useState<string | null>(null);
  const [prediction, setPrediction] = useState<Prediction | null>(null);
  const [error, setError] = useState<string | null>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  /** Reloads the queue and claims the next record. */
  const next = useCallback(async () => {
    try {
      const records = await getUnlabelledRecords(aws);
      setRemaining(records.length);
      queueRef.current = records;
      setCurrent(records.length ? await claimRecord(aws, queueRef.current, username) : null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [aws, username]);

  useEffect(() => {
    void next();
  }, [next]);

  // Fetch the claimed image.
  useEffect(() => {
    if (!current) return;
    let url: string | undefined;
    let cancelled = false;
    setPicture(null);
    setPrediction(null);
    getTrainingImage(aws, current)
      .then((blob) => {
        if (cancelled) return;
        url = URL.createObjectURL(blob);
        setPicture(url);
      })
      .catch(
        (err) => !cancelled && setError(`Failed to load image: ${err instanceof Error ? err.message : err}`),
      );
    return () => {
      cancelled = true;
      if (url) URL.revokeObjectURL(url);
    };
  }, [aws, current]);

  // What the model sees: the largest centred square, scaled to 240×240 (like tf resize_with_crop_or_pad).
  useEffect(() => {
    const ctx = canvasRef.current?.getContext('2d');
    if (!picture || !ctx) return;
    const image = new Image();
    image.onload = () => {
      const size = Math.min(image.width, image.height);
      ctx.drawImage(image, (image.width - size) / 2, (image.height - size) / 2, size, size, 0, 0, 240, 240);
    };
    image.src = picture;
  }, [picture]);

  // Optional local classifier (bootboots local-training serve.py); silently absent if not running.
  useEffect(() => {
    if (!picture) return;
    const controller = new AbortController();
    (async () => {
      try {
        const body = await (await fetch(picture)).arrayBuffer();
        const response = await fetch(config.localPredictUrl, {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body,
          signal: controller.signal,
        });
        if (response.ok) setPrediction(await response.json());
      } catch {
        // server not running, or cancelled
      }
    })();
    return () => controller.abort();
  }, [picture, config.localPredictUrl]);

  const saving = useRef(false);
  const label = useCallback(
    async (cat: string) => {
      if (!current || saving.current) return;
      saving.current = true;
      setPicture(null);
      try {
        await labelRecord(aws, current, cat);
        setCurrent(null);
        await next();
      } catch (err) {
        setError(`Failed to save label: ${err instanceof Error ? err.message : err}`);
      } finally {
        saving.current = false;
      }
    },
    [aws, current, next],
  );

  useEffect(() => {
    const onKeyUp = (e: KeyboardEvent) => {
      // Leave browser and OS shortcuts (Cmd+C, Ctrl+T, …) alone.
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      const target = e.target as HTMLElement | null;
      if (target && ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName)) return;
      const match = LABELS.find((l) => l.key === e.key);
      if (!match || !picture) return;
      e.preventDefault();
      void label(match.cat);
    };
    window.addEventListener('keyup', onKeyUp);
    return () => window.removeEventListener('keyup', onKeyUp);
  }, [label, picture]);

  if (error) {
    return (
      <Alert severity="error" onClose={() => setError(null)}>
        {error}
      </Alert>
    );
  }
  if (!picture) {
    return (
      <Paper variant="outlined" sx={{ p: 6, textAlign: 'center' }}>
        <Typography>{remaining === 0 ? "We're all out of Kitties!" : 'Loading the next Kitty...'}</Typography>
      </Paper>
    );
  }

  return (
    <div>
      <Typography variant="h6" gutterBottom>
        Giving Boots the boot. Only {remaining === 1 ? '1 image' : `${remaining} images`} left to go!
      </Typography>
      <Paper variant="outlined" sx={{ p: 2 }}>
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: 16,
            flexWrap: 'wrap',
          }}
        >
          <div style={{ display: 'inline-flex', flexDirection: 'column', maxWidth: '100%', minWidth: 0 }}>
            <img
              src={picture}
              alt="Cat"
              style={{
                maxWidth: '100%',
                maxHeight: 'calc(100vh - 420px)',
                minHeight: 240,
                objectFit: 'contain',
              }}
            />
            <div
              style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 6 }}
            >
              <Typography variant="caption" color="text.secondary" fontFamily="monospace">
                {current ? (parseImageDate(current.imageName) ?? current.imageName.split('/').pop()) : ''}
              </Typography>
              {prediction && (
                <span
                  style={{
                    background: badgeColour(prediction),
                    color: 'white',
                    padding: '4px 10px',
                    borderRadius: 6,
                    fontSize: 13,
                    fontWeight: 600,
                  }}
                >
                  {prediction.prediction} {Math.round(prediction.confidence * 100)}%
                </span>
              )}
            </div>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
            <canvas
              ref={canvasRef}
              width={240}
              height={240}
              style={{ border: '1px solid #374151', imageRendering: 'pixelated' }}
            />
            <Typography variant="caption" color="text.secondary" fontFamily="monospace" sx={{ mt: 1 }}>
              what the AI sees (240×240)
            </Typography>
          </div>
        </div>
        <div style={{ display: 'flex', flexWrap: 'wrap', justifyContent: 'center', marginTop: 12 }}>
          {LABELS.map((l) => (
            <Button
              key={l.cat}
              title={l.title}
              aria-label={l.title}
              onClick={() => label(l.cat)}
              sx={{
                width: 100,
                height: 100,
                p: 0,
                m: 0.5,
                bgcolor: '#3b4048',
                color: '#dcdfe4',
                minWidth: 0,
              }}
            >
              {l.content}
            </Button>
          ))}
        </div>
      </Paper>
    </div>
  );
}
