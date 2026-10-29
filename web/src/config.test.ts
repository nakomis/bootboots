import { configUrl, loadConfig, parseConfig } from './config';
import { config } from './test/render';

const { localPredictUrl: _predict, samServerUrl: _sam, ...required } = config;

afterEach(() => vi.unstubAllGlobals());

describe('parseConfig', () => {
  test('accepts a full config', () => {
    expect(parseConfig(config)).toEqual(config);
  });

  test('defaults the optional fields', () => {
    expect(parseConfig(required)).toEqual({
      ...required,
      samServerUrl: '',
      localPredictUrl: 'http://localhost:8765/predict',
    });
  });

  test('keeps overrides of optional fields', () => {
    expect(
      parseConfig({ ...required, localPredictUrl: 'http://x/y', samServerUrl: 'https://sam' }),
    ).toMatchObject({ localPredictUrl: 'http://x/y', samServerUrl: 'https://sam' });
  });

  test('names every missing or empty required field', () => {
    const { region: _region, version: _version, ...rest } = required;
    expect(() => parseConfig({ ...rest, identityPoolId: '' })).toThrow(
      'config.json is missing required fields: version, region, identityPoolId',
    );
  });

  test.each([null, 'text', 42, []])('rejects %j', (raw) => {
    expect(() => parseConfig(raw)).toThrow('config.json must be a JSON object');
  });
});

describe('loadConfig', () => {
  const url = 'https://app.example/remotes/bootboots/config.json';

  test('fetches uncached and returns the validated config', async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify(config)));
    vi.stubGlobal('fetch', fetchMock);
    await expect(loadConfig(url)).resolves.toEqual(config);
    expect(fetchMock).toHaveBeenCalledWith(url, { cache: 'no-cache' });
  });

  test('fails on a missing field', async () => {
    const { project: _project, ...rest } = required;
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response(JSON.stringify(rest))),
    );
    await expect(loadConfig(url)).rejects.toThrow('missing required fields: project');
  });

  test('fails on an HTTP error', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response('nope', { status: 404 })),
    );
    await expect(loadConfig(url)).rejects.toThrow('HTTP 404');
  });

  test('fails when the network fails', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => Promise.reject(new TypeError('Failed to fetch'))),
    );
    await expect(loadConfig(url)).rejects.toThrow(`Could not fetch ${url}: Failed to fetch`);
  });

  test('fails on invalid JSON', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response('<html>')),
    );
    await expect(loadConfig(url)).rejects.toThrow('is not valid JSON');
  });
});

describe('configUrl', () => {
  test('resolves beside remoteEntry.js from a built chunk in assets/', () => {
    expect(configUrl('https://app.example/remotes/bootboots/assets/ShellApp-abc.js')).toBe(
      'https://app.example/remotes/bootboots/config.json',
    );
  });

  test('resolves to the dev server root from src/', () => {
    expect(configUrl('http://localhost:5175/src/config.ts')).toBe('http://localhost:5175/config.json');
  });
});
