import { fireEvent, screen, waitFor } from '@testing-library/react';
import * as bounding from '../services/bounding';
import * as images from '../services/images';
import * as sam from '../services/sam';
import { renderWithBootboots } from '../test/render';
import { BoundingPage } from './BoundingPage';

beforeAll(() => {
  URL.createObjectURL = vi.fn(() => 'blob:cat');
  URL.revokeObjectURL = vi.fn();
  // jsdom has no layout: give the image a size so the canvas appears.
  Object.defineProperty(HTMLImageElement.prototype, 'clientWidth', { configurable: true, get: () => 400 });
  Object.defineProperty(HTMLImageElement.prototype, 'clientHeight', { configurable: true, get: () => 300 });
  HTMLCanvasElement.prototype.getContext = vi.fn(() => null) as never;
});
afterEach(() => vi.restoreAllMocks());

const stats = Object.fromEntries(bounding.BOUNDABLE_CATS.map((c) => [c, { sorted: 4, bounded: 1 }]));

test('overview, annotate with SAM, save, and handle the empty queue', async () => {
  vi.spyOn(bounding, 'getBoundingStats').mockResolvedValue(stats);
  const claim = vi
    .spyOn(bounding, 'claimNextUnbounded')
    .mockResolvedValueOnce({ imageName: 'a.jpg', uuid: '1', cat: 'Mu' })
    .mockResolvedValueOnce(null);
  vi.spyOn(images, 'getTrainingImage').mockResolvedValue(new Blob(['x']));
  vi.spyOn(sam, 'segment').mockResolvedValue({
    overlay: 'png',
    bounding_box: { xmin: 1, ymin: 2, xmax: 3, ymax: 4 },
  });
  const save = vi.spyOn(bounding, 'saveBoundingData').mockResolvedValue();

  renderWithBootboots(<BoundingPage />);
  expect(await screen.findAllByText('4 sorted')).toHaveLength(6);
  fireEvent.click(screen.getByText('Mu'));
  await waitFor(() => expect(claim).toHaveBeenCalledWith(expect.anything(), 'Mu', 'martin'));

  const img = await screen.findByAltText('annotate');
  fireEvent.load(img);
  const canvas = await screen.findByTestId('bounding-canvas');
  expect(screen.getByRole('button', { name: 'Save ✓' })).toBeDisabled();
  fireEvent.pointerDown(canvas, { button: 0, clientX: 10, clientY: 20 });
  fireEvent.contextMenu(canvas, { clientX: 30, clientY: 40 });
  await waitFor(() => expect(sam.segment).toHaveBeenCalledTimes(2));
  const save$ = screen.getByRole('button', { name: 'Save ✓' });
  await waitFor(() => expect(save$).toBeEnabled());
  fireEvent.keyDown(window, { key: 's' });
  await waitFor(() => expect(save).toHaveBeenCalled());
  expect(save.mock.calls[0][3]).toEqual([
    { x: 10, y: 20, label: 1 },
    { x: 30, y: 40, label: 0 },
  ]);
  expect(await screen.findByText('All Mu images are bounded! 🎉')).toBeInTheDocument();

  fireEvent.click(screen.getByRole('button', { name: '← Overview' }));
  expect(await screen.findByText('Cat Bounding')).toBeInTheDocument();
});

test('"not this cat", skip, reset and SAM errors', async () => {
  vi.spyOn(bounding, 'getBoundingStats').mockResolvedValue(stats);
  vi.spyOn(bounding, 'claimNextUnbounded').mockResolvedValue({ imageName: 'a.jpg', uuid: '1', cat: 'Tau' });
  vi.spyOn(images, 'getTrainingImage').mockResolvedValue(new Blob(['x']));
  vi.spyOn(sam, 'segment').mockRejectedValue(new Error('SAM server error: 502'));
  const unlabel = vi.spyOn(bounding, 'unlabelRecord').mockResolvedValue();

  renderWithBootboots(<BoundingPage />);
  fireEvent.click(await screen.findByText('Tau'));
  fireEvent.load(await screen.findByAltText('annotate'));
  fireEvent.click(screen.getByRole('button', { name: '● Background' }));
  fireEvent.pointerDown(await screen.findByTestId('bounding-canvas'), { button: 0, clientX: 1, clientY: 1 });
  expect(await screen.findByText('SAM server error: 502')).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Reset' }));
  expect(screen.queryByText('SAM server error: 502')).not.toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Skip' }));
  fireEvent.click(await screen.findByRole('button', { name: "That's not Tau!" }));
  await waitFor(() => expect(unlabel).toHaveBeenCalled());
});

test('shows stats and record errors', async () => {
  vi.spyOn(bounding, 'getBoundingStats').mockRejectedValue(new Error('denied'));
  vi.spyOn(bounding, 'claimNextUnbounded').mockRejectedValue(new Error('scan failed'));
  vi.spyOn(console, 'error').mockImplementation(() => {});
  renderWithBootboots(<BoundingPage />);
  expect(await screen.findByText('Failed to load stats: denied')).toBeInTheDocument();
  fireEvent.click(screen.getByText('Boots'));
  expect(await screen.findByText('Error: scan failed')).toBeInTheDocument();
});
