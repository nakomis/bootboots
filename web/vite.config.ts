/// <reference types="vitest/config" />
import { federation } from '@module-federation/vite';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// Built as a Module Federation remote served from app.sandbox.nakomis.com/remotes/bootboots/ by nakomis-shell (sandbox only).
export default defineConfig(({ command }) => ({
  base: command === 'build' ? '/remotes/bootboots/' : '/',
  plugins: [
    react(),
    federation({
      name: 'bootboots',
      filename: 'remoteEntry.js',
      exposes: { './App': './src/ShellApp.tsx' },
      shared: {
        react: { singleton: true },
        'react-dom': { singleton: true },
      },
      dts: false,
    }),
  ],
  build: { target: 'es2022' },
  server: { port: 5175, origin: 'http://localhost:5175' },
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      include: ['src/**/*.{ts,tsx}'],
      exclude: [
        'src/**/*.test.{ts,tsx}',
        'src/test/**',
        'src/main.tsx',
        'src/DevHarness.tsx',
        'src/vite-env.d.ts',
      ],
      thresholds: { lines: 70 },
    },
  },
}));
