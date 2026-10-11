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
  let cached: Promise<AwsCredentialIdentity> | undefined;
  let expiresAt = 0;

  const fetchFresh = async (): Promise<AwsCredentialIdentity> => {
    if (!identityPoolId) throw new Error('BootBoots identity pool is not configured in this build');
    const c = await shell.getAwsCredentials(identityPoolId);
    expiresAt = c.expiration.getTime();
    return {
      accessKeyId: c.accessKeyId,
      secretAccessKey: c.secretAccessKey,
      sessionToken: c.sessionToken,
      expiration: c.expiration,
    };
  };

  return async () => {
    if (!cached || expiresAt - now() <= REFRESH_MARGIN_MS) {
      const attempt = fetchFresh();
      cached = attempt;
      attempt.catch(() => {
        if (cached === attempt) cached = undefined;
      });
    }
    return cached;
  };
}
