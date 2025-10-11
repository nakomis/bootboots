# Infrastructure Changelog

## 2025-10-11 - Firmware Cleanup Lambda

### Added

**New Stack: `BootBootsFirmwareCleanupStack`**
- Automated S3 firmware version cleanup
- Keeps only the 3 most recent versions per project
- Automatically updates manifest.json files

**Components:**
- Lambda function: `FirmwareCleanupLambda`
  - Runtime: Node.js 22.x
  - Timeout: 60 seconds
  - Memory: 256 MB
  - Trigger: S3 ObjectCreated events for `firmware.bin` files

- S3 Event Notification
  - Configured automatically via `bucket.addEventNotification()`
  - Triggers on: `s3.EventType.OBJECT_CREATED`
  - Filter: Suffix `firmware.bin`
  - Bucket: `bootboots-firmware-updates`

- CloudWatch Logs
  - Log group: `/aws/lambda/FirmwareCleanupLambda`
  - Retention: 6 months

**IAM Permissions:**
- S3: ListBucket, GetObject, DeleteObject, PutObject
- Lambda invoke permission (automatically granted via `LambdaDestination`)

### Key Features

1. **Semantic Version Sorting**
   - Properly handles version numbers (1.10.0 > 1.9.0)
   - Newest versions kept, oldest deleted

2. **Multi-Project Support**
   - Works for any project following `{Project}/{Version}/firmware.bin` pattern
   - Automatically detects project name from S3 key

3. **Automatic Manifest Updates**
   - Rebuilds manifest.json after cleanup
   - Includes version, timestamp, path, and size metadata

4. **Fully Automated Setup**
   - S3 event notification configured via `bucket.addEventNotification()`
   - Lambda permissions automatically granted via `LambdaDestination`
   - No manual AWS console configuration required

### Changed

**Upload Script Simplified:**
- Removed `create_version_manifest()` method from `build_and_upload.py`
- Manifest updates now handled automatically by Lambda
- Reduced code complexity by ~50 lines

### Files Created

```
infra/
├── lambda/
│   └── firmware-cleanup/
│       ├── src/
│       │   └── handler.ts          # Lambda function handler
│       ├── package.json             # Dependencies
│       ├── tsconfig.json            # TypeScript config
│       └── README.md                # Lambda documentation
├── lib/
│   └── firmware-cleanup-stack.ts   # CDK stack definition
├── DEPLOYMENT.md                    # Deployment guide
└── CHANGELOG.md                     # This file

embedded/catcam/cpp/catcam/scripts/
└── build_and_upload.py             # Modified (simplified)
```

### Files Modified

```
infra/
├── bin/infra.ts                     # Added new stack

embedded/catcam/cpp/catcam/scripts/
└── build_and_upload.py              # Removed manifest logic
```

### Deployment

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/infra
npm install
export AWS_PROFILE=nakom.is-sandbox
cdk deploy BootBootsFirmwareCleanupStack
```

### Benefits

1. **Cost Savings:** Reduces S3 storage costs by automatically deleting old versions
2. **Automation:** No manual S3 management required
3. **Simplified Code:** Upload script is simpler and more focused
4. **Consistent State:** Manifest always matches S3 contents
5. **Multi-Project:** Works for all projects in the bucket

### Technical Implementation

**S3 Event Notification via addEventNotification:**
```typescript
firmwareBucket.addEventNotification(
  s3.EventType.OBJECT_CREATED,
  new s3n.LambdaDestination(cleanupLambda),
  { suffix: 'firmware.bin' }
);
```

Since CDK v2, `addEventNotification()` works on imported buckets (from `Bucket.fromBucketName()`). The `LambdaDestination` automatically:
- Grants S3 permission to invoke the Lambda
- Configures the S3 bucket notification
- Handles cleanup on stack deletion

### Testing

1. Upload firmware: `python scripts/build_and_upload.py`
2. Check CloudWatch Logs: `aws logs tail /aws/lambda/FirmwareCleanupLambda --follow`
3. Verify manifest: `aws s3 cp s3://bootboots-firmware-updates/BootBoots/manifest.json -`
4. List S3 files: `aws s3 ls s3://bootboots-firmware-updates/BootBoots/ --recursive`

Should see only 3 firmware versions retained.

### Migration Notes

**Before this change:**
- Upload script manually updated manifest.json
- Old firmware versions accumulated in S3
- Manual cleanup required

**After this change:**
- Lambda automatically updates manifest.json
- Only 3 most recent versions kept
- Fully automated cleanup

**No migration required** - Next firmware upload will trigger automatic cleanup of existing old versions.

### Known Limitations

1. Requires firmware to follow `{Project}/{Version}/firmware.bin` naming pattern
2. Version must be semantic (MAJOR.MINOR.PATCH)
3. S3 bucket must allow PutBucketNotification API calls

### Future Enhancements

Potential improvements:
- Configurable retention (3 versions is hardcoded)
- Support for pre-release versions (e.g., 1.0.0-beta)
- Notification on cleanup (SNS/email)
- Metrics/dashboards for cleanup activity
