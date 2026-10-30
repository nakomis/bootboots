import type { BoundingBox, BoundingPoint } from './types';

export async function blobToBase64(blob: Blob): Promise<string> {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary);
}

/** Asks the Segment Anything server for a mask overlay and bounding box from the tapped points. */
export async function segment(
  serverUrl: string,
  image: Blob,
  points: BoundingPoint[],
  displayWidth: number,
  displayHeight: number,
  fetchImpl: typeof fetch = fetch,
): Promise<{ overlay: string; bounding_box: BoundingBox }> {
  const response = await fetchImpl(`${serverUrl}/segment`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      image: await blobToBase64(image),
      points,
      display_width: displayWidth,
      display_height: displayHeight,
    }),
  });
  if (!response.ok) throw new Error(`SAM server error: ${response.status}`);
  return response.json();
}
