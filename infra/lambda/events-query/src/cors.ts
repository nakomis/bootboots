// Browser origins allowed to call the events API, per environment, from infra/lib/env-config.ts
// (apiCorsOrigins), passed in as a comma-separated ALLOWED_ORIGINS environment variable.
export function allowedOrigins(): string[] {
    return (process.env.ALLOWED_ORIGINS ?? '').split(',').map((o) => o.trim()).filter(Boolean);
}

/** CORS response headers: echoes the request Origin only when it is on the allow-list. */
export function corsHeadersFor(headers: Record<string, string | undefined> | null | undefined): Record<string, string> {
    const origin = Object.entries(headers ?? {}).find(([k]) => k.toLowerCase() === 'origin')?.[1];
    const result: Record<string, string> = {
        'Access-Control-Allow-Methods': 'GET, OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type, Authorization',
        'Content-Type': 'application/json',
        Vary: 'Origin',
    };
    if (origin && allowedOrigins().includes(origin)) result['Access-Control-Allow-Origin'] = origin;
    return result;
}
