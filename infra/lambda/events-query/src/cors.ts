// Browser origins allowed to call the events API. The nakomis-shell BootBoots panel lives at
// app.sandbox.nakomis.com; the legacy sandbox site stays until it is retired.
export const ALLOWED_ORIGINS = [
    'https://sandbox.nakomis.com',
    'https://app.sandbox.nakomis.com',
    'http://localhost:3000',
    'http://localhost:3001',
];

/** CORS response headers: echoes the request Origin only when it is on the allow-list. */
export function corsHeadersFor(headers: Record<string, string | undefined> | null | undefined): Record<string, string> {
    const origin = Object.entries(headers ?? {}).find(([k]) => k.toLowerCase() === 'origin')?.[1];
    const result: Record<string, string> = {
        'Access-Control-Allow-Methods': 'GET, OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type, Authorization',
        'Content-Type': 'application/json',
        Vary: 'Origin',
    };
    if (origin && ALLOWED_ORIGINS.includes(origin)) result['Access-Control-Allow-Origin'] = origin;
    return result;
}
