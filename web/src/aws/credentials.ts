import type { AwsCredentialIdentity, AwsCredentialIdentityProvider } from '@smithy/types';
import type { ShellContext } from '../shell-contract';

const REFRESH_MARGIN_MS = 5 * 60 * 1000;

/**
 * An AWS SDK v3 credential provider backed by the shell's identity-pool exchange. Credentials are reused
 * until five minutes before they expire, then fetched again (the shell renews the id token as needed).
 */
export function shellCredentials(
  shell: Pick<ShellContext, 'getAwsCredentials'>,
  identityPoolId: string,
  now: () => number = Date.now,
): AwsCredentialIdentityProvider {
  /** The current (or in-flight) fetch. Concurrent callers share it rather than starting another. */
  let cached: { promise: Promise<AwsCredentialIdentity>; expiresAt: number; settled: boolean } | undefined;

  const fetchFresh = async (): Promise<AwsCredentialIdentity> => {
    if (!identityPoolId) throw new Error('BootBoots identity pool is not configured in this build');
    const c = await shell.getAwsCredentials(identityPoolId);
    return {
      accessKeyId: c.accessKeyId,
      secretAccessKey: c.secretAccessKey,
      sessionToken: c.sessionToken,
      expiration: c.expiration,
    };
  };

  return async () => {
    const stale = !cached || (cached.settled && cached.expiresAt - now() <= REFRESH_MARGIN_MS);
    if (stale) {
      const entry = { promise: fetchFresh(), expiresAt: 0, settled: false };
      cached = entry;
      entry.promise.then(
        (c) => {
          entry.expiresAt = c.expiration?.getTime() ?? 0;
          entry.settled = true;
        },
        () => {
          // Retry on the next call.
          if (cached === entry) cached = undefined;
        },
      );
    }
    return (cached as NonNullable<typeof cached>).promise;
  };
}
