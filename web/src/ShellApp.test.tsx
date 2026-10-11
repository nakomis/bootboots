import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import ShellApp from './ShellApp';
import * as aws from './services/aws';
import * as events from './services/events';
import * as fleet from './services/fleet';
import { shell } from './test/render';

afterEach(() => vi.restoreAllMocks());

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

test('a fresh ShellContext object from the shell does not rebuild services or reopen the WebSocket', async () => {
  const sockets: string[] = [];
  class FakeWebSocket {
    onopen?: () => void;
    onclose?: () => void;
    onmessage?: () => void;
    constructor(url: string) {
      sockets.push(url);
    }
    send() {}
    close() {}
  }
  vi.stubGlobal('WebSocket', FakeWebSocket);
  const awsSpy = vi.spyOn(aws, 'createAwsServices');
  const fleetSpy = vi.spyOn(fleet, 'createFleetApi');
  vi.stubGlobal(
    'fetch',
    vi.fn(async () => new Response(JSON.stringify({ devices: [] }))),
  );
  window.history.replaceState({}, '', '/bootboots/remote');
  const getAccessToken = vi.fn(async () => 'first');
  const { rerender } = render(<ShellApp {...shell} getAccessToken={getAccessToken} />);
  await waitFor(() => expect(sockets).toHaveLength(1));

  const latest = vi.fn(async () => 'second');
  rerender(<ShellApp {...{ ...shell, user: { ...shell.user } }} getAccessToken={latest} />);
  rerender(<ShellApp {...{ ...shell, user: { ...shell.user } }} getAccessToken={latest} />);
  await new Promise((r) => setTimeout(r, 20));

  expect(awsSpy).toHaveBeenCalledTimes(1);
  expect(fleetSpy).toHaveBeenCalledTimes(1);
  expect(sockets).toEqual(['wss://ws.fleet.sandbox.nakomis.com?token=first']);
  // Calls made after the re-render use the shell's latest token function.
  await fleetSpy.mock.results[0].value.listDevices();
  expect(latest).toHaveBeenCalled();
  vi.unstubAllGlobals();
});
