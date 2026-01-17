# Claude Code Guidelines for BootBoots Infrastructure

## AWS Configuration

Always use the sandbox profile when working with AWS CLI:

```bash
export AWS_PROFILE=nakom.is-sandbox
```

## File Locations

- **CDK stacks**: `lib/` directory

## IoT Resources

The `lib/iot-device.ts` stack uses `ThingWithCert` from `cdk-iot-core-certificates-v3` and creates:
- IoT Thing (`BootBootsThing`)
- Certificate (auto-generated, stored in SSM Parameter Store)
- IAM Role and Policy for device access
- Role Alias for credential provider
- IoT Policy for assuming role with certificate

## SSM Parameters

Credentials are stored in SSM Parameter Store with the `BootsBoots` prefix:
- `/BootsBoots/cert_pem` - Certificate PEM
- `/BootsBoots/priv_key` - Private key
