import Box from '@mui/material/Box';
import Tab from '@mui/material/Tab';
import Tabs from '@mui/material/Tabs';
import { Link, Navigate, Route, Routes, useLocation } from 'react-router';
import { BoundingPage } from './pages/BoundingPage';
import { CatchesPage } from './pages/CatchesPage';
import { DevicePage } from './pages/DevicePage';
import { LabellingPage } from './pages/LabellingPage';
import { RemotePage } from './pages/RemotePage';

const TABS = [
  { path: '/catches', label: 'Catches' },
  { path: '/labelling', label: 'Labelling' },
  { path: '/bounding', label: 'Bounding' },
  { path: '/remote', label: 'Remote' },
  { path: '/device', label: 'Bluetooth' },
];

export function BootbootsRoutes() {
  const { pathname } = useLocation();
  const tab = TABS.find((t) => pathname.startsWith(t.path))?.path ?? false;

  return (
    <Box>
      <Box sx={{ borderBottom: 1, borderColor: 'divider', mb: 2 }}>
        <Tabs value={tab} variant="scrollable" allowScrollButtonsMobile>
          {TABS.map((t) => (
            <Tab key={t.path} label={t.label} value={t.path} component={Link} to={t.path} />
          ))}
        </Tabs>
      </Box>
      <Routes>
        <Route path="/" element={<Navigate to="/catches" replace />} />
        <Route path="/catches" element={<CatchesPage />} />
        <Route path="/labelling" element={<LabellingPage />} />
        <Route path="/bounding" element={<BoundingPage />} />
        <Route path="/remote" element={<RemotePage />} />
        <Route path="/device" element={<DevicePage />} />
        <Route path="*" element={<Navigate to="/catches" replace />} />
      </Routes>
    </Box>
  );
}
