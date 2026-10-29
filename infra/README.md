# BootBoots infrastructure

A single AWS CDK app (TypeScript, pnpm) for every BootBoots stack, deployed per environment.
See [CLAUDE.md](CLAUDE.md) for the stacks, environments, scripts and deploy rules.

```bash
pnpm install
pnpm test
pnpm diff-sandbox BootBootsApiGatewayStack     # always diff first
pnpm deploy-sandbox BootBootsApiGatewayStack   # deploy by stack name
```
