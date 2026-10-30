import Link from '@mui/material/Link';
import type { MouseEvent, ReactNode } from 'react';

/**
 * A link into the Fleet remote. It lives outside this remote's router basename, so navigate the shell's
 * router (which listens for popstate) instead of reloading the page.
 */
export function FleetLink({ deviceId, children }: { deviceId: string; children: ReactNode }) {
  const href = `/fleet/devices/${encodeURIComponent(deviceId)}`;
  const onClick = (e: MouseEvent<HTMLAnchorElement>) => {
    if (e.metaKey || e.ctrlKey || e.shiftKey || e.button !== 0) return;
    e.preventDefault();
    window.history.pushState({}, '', href);
    window.dispatchEvent(new PopStateEvent('popstate'));
  };
  return (
    <Link href={href} onClick={onClick}>
      {children}
    </Link>
  );
}
