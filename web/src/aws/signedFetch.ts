import { Sha256 } from '@aws-crypto/sha256-js';
import { SignatureV4 } from '@smithy/signature-v4';
import type { AwsCredentialIdentityProvider } from '@smithy/types';

/** fetch() with a SigV4 signature for API Gateway IAM auth (execute-api). */
export async function signedFetch(
  url: string,
  credentials: AwsCredentialIdentityProvider,
  region: string,
  init: { method?: string; headers?: Record<string, string>; body?: string } = {},
  fetchImpl: typeof fetch = fetch,
): Promise<Response> {
  const signer = new SignatureV4({ credentials, region, service: 'execute-api', sha256: Sha256 });
  const u = new URL(url);
  const query: Record<string, string> = {};
  u.searchParams.forEach((value, key) => {
    query[key] = value;
  });
  const signed = await signer.sign({
    method: init.method ?? 'GET',
    protocol: u.protocol,
    hostname: u.hostname,
    port: u.port ? Number(u.port) : undefined,
    path: u.pathname,
    query,
    headers: { host: u.host, ...(init.headers ?? {}) },
    body: init.body,
  });
  // Browsers set Host themselves and refuse to let scripts send it.
  const { host: _host, ...headers } = signed.headers;
  return fetchImpl(url, { method: init.method ?? 'GET', headers, body: init.body });
}
