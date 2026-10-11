import { shellCredentials } from './credentials';

test('reuses credentials until five minutes before expiry, then refreshes', async () => {
  let now = 0;
  const getAwsCredentials = vi.fn(async (pool: string) => ({
    accessKeyId: `AK-${pool}-${getAwsCredentials.mock.calls.length}`,
    secretAccessKey: 's',
    sessionToken: 't',
    expiration: new Date(now + 60 * 60 * 1000),
  }));
  const provider = shellCredentials({ getAwsCredentials }, 'pool', () => now);
  expect((await provider()).accessKeyId).toBe('AK-pool-1');
  now = 50 * 60 * 1000;
  expect((await provider()).accessKeyId).toBe('AK-pool-1');
  now = 56 * 60 * 1000;
  expect((await provider()).accessKeyId).toBe('AK-pool-2');
});

test('a failed exchange is retried on the next call', async () => {
  const getAwsCredentials = vi
    .fn()
    .mockRejectedValueOnce(new Error('expired'))
    .mockResolvedValueOnce({
      accessKeyId: 'ok',
      secretAccessKey: 's',
      sessionToken: 't',
      expiration: new Date(9e15),
    });
  const provider = shellCredentials({ getAwsCredentials }, 'pool');
  await expect(provider()).rejects.toThrow('expired');
  expect((await provider()).accessKeyId).toBe('ok');
});

test('refuses to run without an identity pool', async () => {
  await expect(shellCredentials({ getAwsCredentials: vi.fn() }, '')()).rejects.toThrow('not configured');
});
