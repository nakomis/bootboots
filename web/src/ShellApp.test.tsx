import { fireEvent, render, screen } from '@testing-library/react';
import ShellApp from './ShellApp';
import * as events from './services/events';
import { shell } from './test/render';

test('mounts under the shell basePath with its own router and tabs', async () => {
  vi.spyOn(events, 'getEvents').mockResolvedValue([]);
  window.history.replaceState({}, '', '/bootboots');
  render(<ShellApp {...shell} />);
  expect(await screen.findByRole('tab', { name: 'Catches', selected: true })).toBeInTheDocument();
  expect(window.location.pathname).toBe('/bootboots/catches');
  expect(screen.getByRole('tab', { name: 'Bluetooth' })).toHaveAttribute('href', '/bootboots/device');
  expect(await screen.findByText('No events found above 50.0% confidence.')).toBeInTheDocument();
});

test('the Fleet link navigates the shell router without reloading', () => {
  vi.spyOn(events, 'getEvents').mockResolvedValue([]);
  window.history.replaceState({}, '', '/bootboots/device');
  const popstate = vi.fn();
  window.addEventListener('popstate', popstate);
  render(<ShellApp {...shell} />);
  fireEvent.click(screen.getByRole('link', { name: 'Fleet' }));
  expect(window.location.pathname).toBe('/fleet/devices/BootBootsThing');
  expect(popstate).toHaveBeenCalled();
  window.removeEventListener('popstate', popstate);
});
