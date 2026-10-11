import { fireEvent, screen, waitFor } from '@testing-library/react';
import * as catadata from '../services/catadata';
import * as images from '../services/images';
import { renderWithBootboots } from '../test/render';
import { LabellingPage, parseImageDate } from './LabellingPage';

beforeAll(() => {
  URL.createObjectURL = vi.fn(() => 'blob:cat');
  URL.revokeObjectURL = vi.fn();
});
afterEach(() => vi.restoreAllMocks());

test('parses both image filename formats', () => {
  expect(parseImageDate('catcam-training/2026-03-30T09:57:53.552Z.jpg')).toMatch(
    /^\d\d Mar 26 - \d\d:57:53$/,
  );
  expect(parseImageDate('2025-07-29T21-20-54-225Z.jpg')).toMatch(/^\d\d Jul 25 - \d\d:20:54$/);
  expect(parseImageDate('540.jpeg')).toBeNull();
});

test('claims an image, labels it by button and keyboard, then runs out of kitties', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async () => Promise.reject(new Error('no local classifier'))),
  );
  const record = { imageName: '2026-03-30T09:57:53.552Z.jpg', uuid: '1' };
  const unlabelled = vi
    .spyOn(catadata, 'getUnlabelledRecords')
    .mockResolvedValueOnce([record, { ...record, uuid: '2' }])
    .mockResolvedValueOnce([record])
    .mockResolvedValueOnce([]);
  vi.spyOn(catadata, 'claimRecord').mockImplementation(async (_aws, queue) => queue.pop() ?? null);
  const label = vi.spyOn(catadata, 'labelRecord').mockResolvedValue();
  vi.spyOn(images, 'getTrainingImage').mockResolvedValue(new Blob(['x']));

  renderWithBootboots(<LabellingPage />);
  expect(await screen.findByText(/Only 2 images left to go!/)).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Boots (B)' }));
  await waitFor(() =>
    expect(label).toHaveBeenCalledWith(expect.anything(), expect.objectContaining({ uuid: '2' }), 'Boots'),
  );
  expect(await screen.findByText(/Only 1 image left to go!/)).toBeInTheDocument();

  fireEvent.keyUp(window, { key: 'w' });
  await waitFor(() => expect(label).toHaveBeenLastCalledWith(expect.anything(), expect.anything(), 'Wolf'));
  expect(await screen.findByText("We're all out of Kitties!")).toBeInTheDocument();
  expect(unlabelled).toHaveBeenCalledTimes(3);
  vi.unstubAllGlobals();
});

test('shows the local classifier prediction when it is running', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async (url: string) =>
      url === 'blob:cat'
        ? new Response('img')
        : new Response(JSON.stringify({ prediction: 'Boots', confidence: 0.9 })),
    ),
  );
  vi.spyOn(catadata, 'getUnlabelledRecords').mockResolvedValue([{ imageName: '540.jpeg', uuid: '1' }]);
  vi.spyOn(catadata, 'claimRecord').mockResolvedValue({ imageName: '540.jpeg', uuid: '1' });
  vi.spyOn(images, 'getTrainingImage').mockResolvedValue(new Blob(['x']));
  renderWithBootboots(<LabellingPage />);
  expect(await screen.findByText('Boots 90%')).toBeInTheDocument();
  expect(screen.getByText('540.jpeg')).toBeInTheDocument();
  vi.unstubAllGlobals();
});

test('reports failures', async () => {
  vi.spyOn(catadata, 'getUnlabelledRecords').mockRejectedValue(new Error('AccessDenied'));
  renderWithBootboots(<LabellingPage />);
  expect(await screen.findByText('AccessDenied')).toBeInTheDocument();
});
