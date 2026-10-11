import { fireEvent, screen } from '@testing-library/react';
import * as events from '../services/events';
import { renderWithBootboots } from '../test/render';
import { CatchesPage } from './CatchesPage';

afterEach(() => vi.restoreAllMocks());

test('shows catches and opens the detail dialog', async () => {
  vi.spyOn(events, 'getEvents').mockResolvedValue([
    {
      id: '1',
      timestamp: '2026-10-11T10:00:00Z',
      imageName: 'a.jpg',
      bootsConfidence: 0.92,
      imageUrl: 'https://img/a.jpg',
      claudeResult: { cat: 'Boots', confidence: 'high', reasoning: 'black cat' },
    },
    { id: '2', timestamp: '2026-10-11T11:00:00Z', imageName: 'b.jpg', bootsConfidence: 0.6, imageUrl: null },
  ]);
  renderWithBootboots(<CatchesPage />);
  expect(await screen.findByText('92.0%')).toBeInTheDocument();
  expect(screen.getByText('2 events', { exact: false })).toBeInTheDocument();
  expect(screen.getByText('No image')).toBeInTheDocument();

  fireEvent.click(screen.getByText('92.0%'));
  expect(await screen.findByText('TRIGGERED deterrent')).toBeInTheDocument();
  expect(screen.getByTitle('black cat')).toHaveTextContent('Claude: Boots (high confidence)');
  fireEvent.click(screen.getByRole('button', { name: 'Close' }));

  fireEvent.click(screen.getByText('60.0%'));
  expect(await screen.findByText('Near-miss (below trigger threshold)')).toBeInTheDocument();
});

test('shows errors and the empty state', async () => {
  const spy = vi.spyOn(events, 'getEvents').mockRejectedValueOnce(new Error('Events API error: 403'));
  renderWithBootboots(<CatchesPage />);
  expect(await screen.findByText('Events API error: 403')).toBeInTheDocument();
  spy.mockResolvedValueOnce([]);
  fireEvent.click(screen.getByRole('button', { name: 'Refresh' }));
  expect(await screen.findByText('No events found above 50.0% confidence.')).toBeInTheDocument();
});
