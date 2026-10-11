import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardActionArea from '@mui/material/CardActionArea';
import LinearProgress from '@mui/material/LinearProgress';
import Typography from '@mui/material/Typography';
import type { MouseEvent, PointerEvent } from 'react';
import { useCallback, useEffect, useRef, useState } from 'react';
import { useBootboots } from '../BootbootsContext';
import bootsImg from '../images/boots.png';
import chiImg from '../images/chi.png';
import kappaImg from '../images/kappa.png';
import muImg from '../images/mu.png';
import tauImg from '../images/tau.png';
import {
  BOUNDABLE_CATS,
  type CatBoundingStats,
  claimNextUnbounded,
  getBoundingStats,
  saveBoundingData,
  unlabelRecord,
} from '../services/bounding';
import { getTrainingImage } from '../services/images';
import { segment } from '../services/sam';
import type { BoundingBox, BoundingPoint, CatadataRecord } from '../services/types';

const CAT_IMAGES: Record<string, string> = {
  Boots: bootsImg,
  Chi: chiImg,
  Kappa: kappaImg,
  Mu: muImg,
  Tau: tauImg,
};

type TapMode = 'positive' | 'negative';

const errorText = (err: unknown) => (err instanceof Error ? err.message : String(err));

function StatCard({ cat, stats, onClick }: { cat: string; stats: CatBoundingStats; onClick: () => void }) {
  const src = CAT_IMAGES[cat];
  return (
    <Card variant="outlined" sx={{ minWidth: 140 }}>
      <CardActionArea
        onClick={onClick}
        sx={{ p: 2, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 1 }}
      >
        {src ? (
          <img src={src} alt={cat} style={{ width: 80, height: 80, objectFit: 'cover', borderRadius: 8 }} />
        ) : (
          <div
            style={{
              width: 80,
              height: 80,
              borderRadius: 8,
              background: '#3b4048',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: 32,
            }}
          >
            🚫
          </div>
        )}
        <Typography fontWeight="bold" fontSize={18}>
          {cat}
        </Typography>
        <Typography variant="body2" color="text.secondary">
          {stats.sorted} sorted
        </Typography>
        <Typography variant="body2" color={stats.bounded > 0 ? 'success.main' : 'text.secondary'}>
          {stats.bounded} bounded
        </Typography>
        {stats.sorted > 0 && (
          <LinearProgress
            variant="determinate"
            color="success"
            value={Math.round((stats.bounded / stats.sorted) * 100)}
            sx={{ width: '100%' }}
          />
        )}
      </CardActionArea>
    </Card>
  );
}

/** Cat bounding: tap a cat and background points, SAM draws the mask and box, save it to catadata. */
export function BoundingPage() {
  const { aws, config, username } = useBootboots();
  const [view, setView] = useState<'overview' | 'annotating'>('overview');
  const [stats, setStats] = useState<Record<string, CatBoundingStats>>({});
  const [statsLoading, setStatsLoading] = useState(true);
  const [statsError, setStatsError] = useState<string | null>(null);

  const [cat, setCat] = useState<string | null>(null);
  const [record, setRecord] = useState<CatadataRecord | null>(null);
  const [imageBlob, setImageBlob] = useState<Blob | null>(null);
  const [imageUrl, setImageUrl] = useState<string | null>(null);
  const [canvasSize, setCanvasSize] = useState({ w: 0, h: 0 });
  const [points, setPoints] = useState<BoundingPoint[]>([]);
  const [tapMode, setTapMode] = useState<TapMode>('positive');
  const [overlay, setOverlay] = useState<string | null>(null);
  const [box, setBox] = useState<BoundingBox | null>(null);
  const [samLoading, setSamLoading] = useState(false);
  const [samError, setSamError] = useState<string | null>(null);
  const [recordLoading, setRecordLoading] = useState(false);
  const [recordError, setRecordError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const imageRef = useRef<HTMLImageElement>(null);

  const loadStats = useCallback(() => {
    getBoundingStats(aws)
      .then((s) => {
        setStats(s);
        setStatsError(null);
      })
      .catch((err) => setStatsError(errorText(err)))
      .finally(() => setStatsLoading(false));
  }, [aws]);

  useEffect(loadStats, [loadStats]);

  // Release the object URL when the image changes or the page closes.
  useEffect(() => () => void (imageUrl && URL.revokeObjectURL(imageUrl)), [imageUrl]);

  // Redraw the SAM overlay and tapped points.
  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas?.getContext('2d');
    if (!canvas || !ctx || canvasSize.w === 0) return;
    const drawPoints = () => {
      for (const p of points) {
        ctx.beginPath();
        ctx.arc(p.x, p.y, 10, 0, Math.PI * 2);
        ctx.fillStyle = p.label === 1 ? 'rgba(80,200,80,0.9)' : 'rgba(220,60,60,0.9)';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2;
        ctx.stroke();
      }
    };
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (overlay) {
      const img = new Image();
      img.onload = () => {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
        drawPoints();
      };
      img.src = `data:image/png;base64,${overlay}`;
    } else {
      drawPoints();
    }
  }, [overlay, points, canvasSize]);

  const clearAnnotation = useCallback(() => {
    setPoints([]);
    setOverlay(null);
    setBox(null);
    setSamError(null);
    setCanvasSize({ w: 0, h: 0 });
    setImageUrl(null);
    setImageBlob(null);
  }, []);

  const loadNext = useCallback(
    async (forCat: string) => {
      setRecordLoading(true);
      setRecordError(null);
      clearAnnotation();
      try {
        const next = await claimNextUnbounded(aws, forCat, username);
        setRecord(next);
        if (!next) return;
        const blob = await getTrainingImage(aws, next);
        setImageBlob(blob);
        setImageUrl(URL.createObjectURL(blob));
      } catch (err) {
        console.error('Failed to load record:', err);
        setRecordError(errorText(err));
      } finally {
        setRecordLoading(false);
      }
    },
    [aws, username, clearAnnotation],
  );

  const enter = (forCat: string) => {
    setCat(forCat);
    setView('annotating');
    void loadNext(forCat);
  };

  // Keep the canvas the same size as the displayed image.
  const syncCanvasSize = useCallback(() => {
    const el = imageRef.current;
    if (el && el.clientWidth > 0 && el.clientHeight > 0)
      setCanvasSize({ w: el.clientWidth, h: el.clientHeight });
  }, []);

  useEffect(() => {
    const el = imageRef.current;
    if (!imageUrl || recordLoading || !el || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(syncCanvasSize);
    observer.observe(el);
    return () => observer.disconnect();
  }, [imageUrl, recordLoading, syncCanvasSize]);

  const addPoint = async (point: BoundingPoint) => {
    if (!imageBlob) return;
    const next = [...points, point];
    setPoints(next);
    setSamLoading(true);
    setSamError(null);
    try {
      const result = await segment(config.samServerUrl, imageBlob, next, canvasSize.w, canvasSize.h);
      setOverlay(result.overlay);
      setBox(result.bounding_box);
    } catch (err) {
      setSamError(err instanceof Error ? err.message : 'SAM server unreachable');
    } finally {
      setSamLoading(false);
    }
  };

  const pointAt = (e: { clientX: number; clientY: number }, label: number): BoundingPoint | null => {
    const rect = canvasRef.current?.getBoundingClientRect();
    return rect ? { x: e.clientX - rect.left, y: e.clientY - rect.top, label } : null;
  };

  const onPointerDown = (e: PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0) return;
    const p = pointAt(e, tapMode === 'positive' ? 1 : 0);
    if (p) void addPoint(p);
  };

  const onContextMenu = (e: MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const p = pointAt(e, 0);
    if (p) void addPoint(p);
  };

  const reset = () => {
    setPoints([]);
    setOverlay(null);
    setBox(null);
    setSamError(null);
  };

  const notThisCat = async () => {
    if (!record || !cat) return;
    setSaving(true);
    try {
      await unlabelRecord(aws, record);
      await loadNext(cat);
    } catch (err) {
      setRecordError(errorText(err));
    } finally {
      setSaving(false);
    }
  };

  const save = useCallback(async () => {
    if (!record || !box || !cat) return;
    setSaving(true);
    try {
      await saveBoundingData(aws, record, box, points, username);
      loadStats();
      await loadNext(cat);
    } catch (err) {
      setRecordError(errorText(err));
    } finally {
      setSaving(false);
    }
  }, [aws, record, box, points, username, cat, loadStats, loadNext]);

  const canSave = !!box && !saving && !samLoading;

  // "S" saves while annotating.
  useEffect(() => {
    if (view !== 'annotating') return;
    const onKey = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement | null)?.tagName === 'INPUT') return;
      if ((e.key === 's' || e.key === 'S') && canSave) void save();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [view, save, canSave]);

  if (view === 'overview') {
    return (
      <div>
        <Typography variant="h5" gutterBottom>
          Cat Bounding
        </Typography>
        <Typography color="text.secondary" sx={{ mb: 3 }}>
          Tap a cat to start adding bounding boxes to their images.
        </Typography>
        {statsError && (
          <Alert severity="error" sx={{ mb: 2 }}>
            Failed to load stats: {statsError}
          </Alert>
        )}
        {statsLoading ? (
          <Typography color="text.secondary">Loading stats…</Typography>
        ) : (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
            {BOUNDABLE_CATS.map((c) => (
              <StatCard
                key={c}
                cat={c}
                stats={stats[c] ?? { sorted: 0, bounded: 0 }}
                onClick={() => enter(c)}
              />
            ))}
          </div>
        )}
      </div>
    );
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, flexWrap: 'wrap' }}>
        <Button
          variant="outlined"
          onClick={() => {
            setView('overview');
            clearAnnotation();
            setRecordError(null);
            setRecord(null);
          }}
        >
          ← Overview
        </Button>
        <Typography fontWeight="bold" fontSize={18}>
          {cat}
        </Typography>
        {cat && stats[cat] && (
          <Typography variant="body2" color="text.secondary">
            {stats[cat].bounded}/{stats[cat].sorted}
          </Typography>
        )}
        {samLoading && (
          <Typography variant="body2" color="text.secondary">
            Segmenting…
          </Typography>
        )}
        {samError && (
          <Typography variant="body2" color="error">
            {samError}
          </Typography>
        )}
      </div>

      <div
        style={{
          position: 'relative',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          minHeight: 240,
        }}
      >
        {recordLoading && <Typography color="text.secondary">Loading image…</Typography>}
        {!recordLoading && recordError && <Alert severity="error">Error: {recordError}</Alert>}
        {!recordLoading && !record && !recordError && (
          <Typography color="text.secondary">All {cat} images are bounded! 🎉</Typography>
        )}
        {!recordLoading && imageUrl && (
          <div style={{ position: 'relative', maxWidth: '100%' }}>
            <img
              ref={imageRef}
              src={imageUrl}
              alt="annotate"
              onLoad={syncCanvasSize}
              draggable={false}
              style={{ display: 'block', maxHeight: 'calc(100vh - 260px)', maxWidth: '100%' }}
            />
            {canvasSize.w > 0 && (
              <canvas
                ref={canvasRef}
                data-testid="bounding-canvas"
                width={canvasSize.w}
                height={canvasSize.h}
                style={{ position: 'absolute', top: 0, left: 0, cursor: 'crosshair', touchAction: 'none' }}
                onPointerDown={onPointerDown}
                onContextMenu={onContextMenu}
              />
            )}
          </div>
        )}
      </div>

      <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap', justifyContent: 'center' }}>
        <Button variant="outlined" onClick={reset} disabled={saving}>
          Reset
        </Button>
        <Button variant="outlined" onClick={() => cat && loadNext(cat)} disabled={saving || recordLoading}>
          Skip
        </Button>
        <Button
          variant={tapMode === 'positive' ? 'contained' : 'outlined'}
          color="success"
          onClick={() => setTapMode('positive')}
        >
          ● Cat
        </Button>
        <Button
          variant={tapMode === 'negative' ? 'contained' : 'outlined'}
          color="error"
          onClick={() => setTapMode('negative')}
        >
          ● Background
        </Button>
        <Button variant="outlined" color="error" onClick={notThisCat} disabled={saving || !record}>
          That's not {cat}!
        </Button>
        <Button variant="contained" color="success" onClick={save} disabled={!canSave}>
          {saving ? 'Saving…' : 'Save ✓'}
        </Button>
      </div>
    </div>
  );
}
