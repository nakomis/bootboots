import { ALLOWED_ORIGINS, corsHeadersFor } from '../lambda/events-query/src/cors';

describe('events API CORS', () => {
    test.each(ALLOWED_ORIGINS)('echoes the allowed origin %s', (origin) => {
        const headers = corsHeadersFor({ Origin: origin });
        expect(headers['Access-Control-Allow-Origin']).toBe(origin);
        expect(headers.Vary).toBe('Origin');
    });

    test('allows the nakomis-shell app origin, whatever the header case', () => {
        expect(corsHeadersFor({ origin: 'https://app.sandbox.nakomis.com' })['Access-Control-Allow-Origin']).toBe(
            'https://app.sandbox.nakomis.com',
        );
    });

    test('omits Allow-Origin for unknown or missing origins', () => {
        expect(corsHeadersFor({ Origin: 'https://evil.example.com' })['Access-Control-Allow-Origin']).toBeUndefined();
        expect(corsHeadersFor(null)['Access-Control-Allow-Origin']).toBeUndefined();
        expect(corsHeadersFor(undefined)['Content-Type']).toBe('application/json');
    });
});
