# Firmware Cleanup Lambda

Automatically manages firmware versions in S3 by keeping only the 3 most recent versions per project and updating the manifest.json file.

## Overview

This Lambda function is triggered whenever a firmware file is uploaded to the `bootboots-firmware-updates` S3 bucket. It automatically:

1. Detects the project name from the S3 key
2. Lists all firmware versions for that project
3. Sorts versions by semantic versioning (newest first)
4. Keeps the top 3 versions
5. Deletes older versions (version 4+)
6. Rebuilds and uploads the project's `manifest.json` file

## Trigger

**S3 Event Notification** (configured automatically by CDK):
- Event: `s3:ObjectCreated`
- Suffix filter: `firmware.bin`
- Bucket: `bootboots-firmware-updates`

The CDK stack uses `bucket.addEventNotification()` to automatically configure the S3 event trigger when deployed.

## S3 Bucket Structure

```
bootboots-firmware-updates/
├── BootBoots/
│   ├── 1.0.54/
│   │   └── firmware.bin
│   ├── 1.0.53/
│   │   └── firmware.bin
│   ├── 1.0.52/
│   │   └── firmware.bin
│   └── manifest.json
├── OtherProject/
│   ├── 2.1.0/
│   │   └── firmware.bin
│   └── manifest.json
```

## Version Sorting

Versions are sorted using semantic versioning:
- Format: `MAJOR.MINOR.PATCH` (e.g., `1.0.54`)
- Sorted numerically, not lexicographically
- Newest versions first

Examples:
- `1.0.54` > `1.0.53` > `1.0.52`
- `2.0.0` > `1.9.99`
- `1.10.0` > `1.9.0` (not `1.9.0` > `1.10.0`)

## Manifest Format

The Lambda generates `{ProjectName}/manifest.json`:

```json
{
  "project": "BootBoots",
  "versions": [
    {
      "version": "1.0.54",
      "timestamp": "2025-10-11T12:00:00.000Z",
      "firmware_path": "BootBoots/1.0.54/firmware.bin",
      "size": 1940992
    },
    {
      "version": "1.0.53",
      "timestamp": "2025-10-10T15:30:00.000Z",
      "firmware_path": "BootBoots/1.0.53/firmware.bin",
      "size": 1938456
    },
    {
      "version": "1.0.52",
      "timestamp": "2025-10-09T10:15:00.000Z",
      "firmware_path": "BootBoots/1.0.52/firmware.bin",
      "size": 1935120
    }
  ]
}
```

## Multi-Project Support

The Lambda automatically detects the project name from the S3 key pattern:

```
{ProjectName}/{Version}/firmware.bin
```

It works for any project following this pattern:
- `BootBoots/1.0.54/firmware.bin` → Project: `BootBoots`
- `CatCam/2.3.1/firmware.bin` → Project: `CatCam`
- `Sensor/1.0.0/firmware.bin` → Project: `Sensor`

## IAM Permissions

The Lambda requires the following S3 permissions:

```json
{
  "Effect": "Allow",
  "Action": [
    "s3:ListBucket",
    "s3:GetObject",
    "s3:GetObjectMetadata",
    "s3:DeleteObject",
    "s3:PutObject"
  ],
  "Resource": [
    "arn:aws:s3:::bootboots-firmware-updates",
    "arn:aws:s3:::bootboots-firmware-updates/*"
  ]
}
```

## CloudWatch Logs

Logs are written to: `/aws/lambda/FirmwareCleanupLambda`

**Log retention:** 6 months

**Example log output:**
```
Firmware Cleanup Lambda triggered
Processing 1 records
Processing: s3://bootboots-firmware-updates/BootBoots/1.0.54/firmware.bin
Project: BootBoots
Found 5 total firmware versions
Sorted versions:
  1. 1.0.54 (BootBoots/1.0.54/firmware.bin)
  2. 1.0.53 (BootBoots/1.0.53/firmware.bin)
  3. 1.0.52 (BootBoots/1.0.52/firmware.bin)
  4. 1.0.51 (BootBoots/1.0.51/firmware.bin)
  5. 1.0.50 (BootBoots/1.0.50/firmware.bin)
Keeping 3 versions, deleting 2 versions
Deleting 2 old firmware versions
✅ Deleted: BootBoots/1.0.51/firmware.bin
✅ Deleted: BootBoots/1.0.50/firmware.bin
Updating manifest for BootBoots with 3 versions
✅ Updated manifest: BootBoots/manifest.json
✅ Completed processing for BootBoots
✅ All records processed
```

## Deployment

The Lambda is deployed via AWS CDK:

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/infra
npm install
export AWS_PROFILE=nakom.is-sandbox
cdk deploy BootBootsFirmwareCleanupStack
```

The S3 event notification is configured automatically during deployment - no manual setup required!

## Integration with Upload Script

The `build_and_upload.py` script no longer updates the manifest. After uploading firmware to S3, the Lambda automatically:

1. Triggers within seconds of upload
2. Cleans up old versions
3. Updates the manifest

## Testing

### Manual Test via AWS CLI

Simulate an S3 upload event:

```bash
aws lambda invoke \
  --function-name FirmwareCleanupLambda \
  --payload file://test-event.json \
  response.json
```

**test-event.json:**
```json
{
  "Records": [
    {
      "eventVersion": "2.1",
      "eventSource": "aws:s3",
      "awsRegion": "eu-west-2",
      "eventTime": "2025-10-11T12:00:00.000Z",
      "eventName": "ObjectCreated:Put",
      "s3": {
        "bucket": {
          "name": "bootboots-firmware-updates"
        },
        "object": {
          "key": "BootBoots/1.0.54/firmware.bin"
        }
      }
    }
  ]
}
```

### Test by Uploading Firmware

```bash
cd /path/to/catcam/project
python scripts/build_and_upload.py --version-type patch
```

Check CloudWatch Logs to verify Lambda execution.

## Error Handling

The Lambda is designed to be resilient:

- **Invalid version format:** Logs warning and skips
- **S3 permission errors:** Logs error and continues
- **Empty project:** No deletions, manifest with 0 versions
- **Multiple simultaneous uploads:** Idempotent operation

## Benefits

1. **Automated cleanup:** No manual S3 management
2. **Cost savings:** Reduces S3 storage costs
3. **Multi-project support:** Works for all projects in bucket
4. **Simplified upload script:** Less code to maintain
5. **Consistent state:** Manifest always matches S3 contents

## Troubleshooting

### Lambda not triggering

1. Check S3 event notification is configured
2. Verify suffix filter is `firmware.bin`
3. Check Lambda permissions for S3 invoke

### Versions not being deleted

1. Check CloudWatch Logs for errors
2. Verify IAM permissions for `s3:DeleteObject`
3. Ensure version format matches `{ProjectName}/{Version}/firmware.bin`

### Manifest not updating

1. Check CloudWatch Logs for errors
2. Verify IAM permissions for `s3:PutObject`
3. Check manifest.json is not read-only

## Development

**Local testing:**
```bash
cd lambda/firmware-cleanup
npm install
npm test
```

**Build:**
```bash
npm run build
```

The Lambda is bundled automatically by CDK using `aws-lambda-nodejs`.
