# Firmware Cleanup Lambda - Deployment Guide

## Prerequisites

1. AWS CLI configured with appropriate credentials
2. AWS CDK CLI installed (`npm install -g aws-cdk`)
3. Node.js 22.x or later
4. Access to the `nakomis-firmware-updates` S3 bucket

## Deployment Steps

### 1. Install Dependencies

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/infra
npm install
```

### 2. Bootstrap CDK (if not already done)

```bash
cdk bootstrap aws://<ACCOUNT_ID>/<REGION>
```

Replace `<ACCOUNT_ID>` and `<REGION>` with your AWS account ID and region.

### 3. Review the Stack

```bash
cdk synth BootBootsFirmwareCleanupStack
```

This will show you the CloudFormation template that will be created.

### 4. Deploy the Stack

```bash
export AWS_PROFILE=nakom.is-sandbox  # or your AWS profile
cdk deploy BootBootsFirmwareCleanupStack
```

**Note:** The deployment will create:
- Lambda function: `FirmwareCleanupLambda`
- CloudWatch Log Group: `/aws/lambda/FirmwareCleanupLambda`
- IAM role with S3 permissions
- S3 event notification (via `addEventNotification`)

### 5. Verify Deployment

After deployment, you'll see output like this:

```
BootBootsFirmwareCleanupStack.CleanupLambdaArn = arn:aws:lambda:eu-west-2:...
BootBootsFirmwareCleanupStack.CleanupLambdaName = FirmwareCleanupLambda
BootBootsFirmwareCleanupStack.LogGroupName = /aws/lambda/FirmwareCleanupLambda
BootBootsFirmwareCleanupStack.NotificationStatus = S3 event notification configured via addEventNotification
BootBootsFirmwareCleanupStack.S3BucketName = nakomis-firmware-updates
```

**Note:** The S3 event notification is configured automatically by CDK using `bucket.addEventNotification()`. No manual setup required!

Check that the Lambda was created:

```bash
aws lambda get-function --function-name FirmwareCleanupLambda
```

Check CloudWatch Logs:

```bash
aws logs describe-log-groups --log-group-name-prefix /aws/lambda/FirmwareCleanupLambda
```

## Testing the Deployment

### Test 1: Upload a Firmware File

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/cpp/catcam
python scripts/build_and_upload.py --version-type patch
```

Expected output:
```
✅ Successfully uploaded firmware v1.0.55 to S3
📝 Note: Manifest will be updated automatically by Lambda
🎉 Ready for OTA deployment via web interface!
```

### Test 2: Check Lambda Execution

View CloudWatch Logs:

```bash
aws logs tail /aws/lambda/FirmwareCleanupLambda --follow
```

Or in AWS Console:
1. CloudWatch → Log groups → `/aws/lambda/FirmwareCleanupLambda`
2. Click on the latest log stream
3. Verify you see logs like:
   ```
   Firmware Cleanup Lambda triggered
   Processing: s3://nakomis-firmware-updates/BootBoots/1.0.55/firmware.bin
   Found 4 total firmware versions
   Keeping 3 versions, deleting 1 versions
   ✅ Deleted: BootBoots/1.0.51/firmware.bin
   ✅ Updated manifest: BootBoots/manifest.json
   ✅ All records processed
   ```

### Test 3: Verify Manifest

Check the manifest was updated:

```bash
aws s3 cp s3://nakomis-firmware-updates/BootBoots/manifest.json - | python -m json.tool
```

Expected output:
```json
{
  "project": "BootBoots",
  "versions": [
    {
      "version": "1.0.55",
      "timestamp": "2025-10-11T12:00:00.000Z",
      "firmware_path": "BootBoots/1.0.55/firmware.bin",
      "size": 1940992
    },
    {
      "version": "1.0.54",
      ...
    },
    {
      "version": "1.0.53",
      ...
    }
  ]
}
```

### Test 4: Verify Old Versions Deleted

List S3 files:

```bash
aws s3 ls s3://nakomis-firmware-updates/BootBoots/ --recursive
```

You should see only 3 firmware versions (plus manifest.json).

## Updating the Lambda

After making code changes:

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/infra
cdk deploy BootBootsFirmwareCleanupStack
```

CDK will automatically rebuild and deploy the updated Lambda code.

## Rollback

To remove the Lambda:

```bash
cdk destroy BootBootsFirmwareCleanupStack
```

**Warning:** This will delete:
- Lambda function
- CloudWatch Logs
- S3 bucket notification configuration (automatically removed)

The S3 bucket and its contents are NOT affected.

## Troubleshooting

### Error: "Unable to validate the following destination configurations"

**Cause:** S3 doesn't have permission to invoke the Lambda.

**Fix:** The Lambda's resource-based policy is automatically created by the CDK stack via `cleanupLambda.addPermission()`. If you see this error, redeploy the stack:

```bash
cdk deploy BootBootsFirmwareCleanupStack --force
```

### Error: "AccessDenied" when deleting S3 objects

**Cause:** Lambda IAM role doesn't have `s3:DeleteObject` permission.

**Fix:** Check the CDK stack - permissions should be automatically granted. If not:

```bash
# Check the Lambda's role
aws lambda get-function --function-name FirmwareCleanupLambda --query 'Configuration.Role'

# Verify the role has S3 permissions
aws iam list-attached-role-policies --role-name <ROLE_NAME>
```

### Lambda not triggering

1. **Check S3 event notification exists:**
   ```bash
   aws s3api get-bucket-notification-configuration --bucket nakomis-firmware-updates
   ```

2. **Check Lambda permissions:**
   ```bash
   aws lambda get-policy --function-name FirmwareCleanupLambda
   ```

3. **Test manually:**
   ```bash
   aws lambda invoke \
     --function-name FirmwareCleanupLambda \
     --payload file://test-event.json \
     response.json
   ```

## Monitoring

### CloudWatch Metrics

Monitor Lambda execution:

```bash
aws cloudwatch get-metric-statistics \
  --namespace AWS/Lambda \
  --metric-name Invocations \
  --dimensions Name=FunctionName,Value=FirmwareCleanupLambda \
  --start-time 2025-10-11T00:00:00Z \
  --end-time 2025-10-11T23:59:59Z \
  --period 3600 \
  --statistics Sum
```

### Set up CloudWatch Alarms (Optional)

Create an alarm for Lambda errors:

```bash
aws cloudwatch put-metric-alarm \
  --alarm-name FirmwareCleanupLambdaErrors \
  --alarm-description "Alert on FirmwareCleanupLambda errors" \
  --metric-name Errors \
  --namespace AWS/Lambda \
  --statistic Sum \
  --period 300 \
  --threshold 1 \
  --comparison-operator GreaterThanThreshold \
  --evaluation-periods 1 \
  --dimensions Name=FunctionName,Value=FirmwareCleanupLambda
```

## Cost Estimation

**Lambda:**
- Execution time: ~2-5 seconds per invocation
- Memory: 256 MB
- Invocations: ~10-50 per month (depends on firmware release frequency)
- **Estimated cost:** < $0.01/month (well within free tier)

**CloudWatch Logs:**
- Log data: ~1 KB per invocation
- Retention: 6 months
- **Estimated cost:** < $0.01/month

**S3:**
- Storage reduction: ~1.9 MB per deleted version
- If releasing daily, saves ~57 MB/month
- **Cost savings:** ~$0.001/month in S3 storage

## Security Considerations

1. **Least Privilege:** Lambda has minimal S3 permissions
2. **No Public Access:** Lambda is not exposed to the internet
3. **CloudWatch Logs:** No sensitive data logged
4. **S3 Event Filter:** Only triggers on `firmware.bin` files

## Next Steps

After deployment:

1. Upload a few firmware versions to test the cleanup
2. Monitor CloudWatch Logs for any issues
3. Set up CloudWatch Alarms for production monitoring
4. Update documentation if you add more projects
