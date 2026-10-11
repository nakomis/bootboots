/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** Baked in by `pnpm build:sandbox` from SSM /nakomis-bootboots/sandbox/identity-pool-id. */
  readonly VITE_BOOTBOOTS_IDENTITY_POOL_ID?: string;
}
