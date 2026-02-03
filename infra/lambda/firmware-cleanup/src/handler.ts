import { S3Event, S3EventRecord } from 'aws-lambda';
import { S3Client, ListObjectsV2Command, DeleteObjectCommand, GetObjectCommand, PutObjectCommand, HeadObjectCommand } from '@aws-sdk/client-s3';

const s3Client = new S3Client({ region: process.env.AWS_REGION || 'us-east-1' });

interface FirmwareVersion {
  version: string;
  key: string;
  size: number;
  lastModified: Date;
  internal?: boolean;  // From S3 object metadata
}

interface ManifestVersion {
  version: string;
  timestamp: string;
  firmware_path: string;
  size: number;
}

interface Manifest {
  project: string;
  versions: ManifestVersion[];
  internal?: boolean;  // If true, project is hidden from UI (e.g., bootloader)
}

/**
 * Parse semantic version string into comparable components
 */
function parseVersion(version: string): [number, number, number] {
  const match = version.match(/^(\d+)\.(\d+)\.(\d+)/);
  if (!match) {
    throw new Error(`Invalid version format: ${version}`);
  }
  return [parseInt(match[1]), parseInt(match[2]), parseInt(match[3])];
}

/**
 * Compare two semantic versions
 * Returns: negative if v1 < v2, 0 if equal, positive if v1 > v2
 */
function compareVersions(v1: string, v2: string): number {
  const [major1, minor1, patch1] = parseVersion(v1);
  const [major2, minor2, patch2] = parseVersion(v2);

  if (major1 !== major2) return major1 - major2;
  if (minor1 !== minor2) return minor1 - minor2;
  return patch1 - patch2;
}

/**
 * Extract project name from S3 key
 * Pattern: {ProjectName}/{Version}/firmware.bin
 */
function extractProjectName(key: string): string {
  const parts = key.split('/');
  if (parts.length < 3 || parts[2] !== 'firmware.bin') {
    throw new Error(`Invalid firmware key format: ${key}`);
  }
  return parts[0];
}

/**
 * Extract version from S3 key
 * Pattern: {ProjectName}/{Version}/firmware.bin
 */
function extractVersion(key: string): string {
  const parts = key.split('/');
  if (parts.length < 3 || parts[2] !== 'firmware.bin') {
    throw new Error(`Invalid firmware key format: ${key}`);
  }
  return parts[1];
}

/**
 * Get metadata for a firmware object to check if it's internal
 */
async function getFirmwareMetadata(bucket: string, key: string): Promise<{ internal?: boolean }> {
  try {
    const command = new HeadObjectCommand({
      Bucket: bucket,
      Key: key,
    });
    const response = await s3Client.send(command);

    // S3 metadata keys are lowercased
    const internal = response.Metadata?.internal === 'true';
    return { internal: internal || undefined };
  } catch (error) {
    console.warn(`Could not get metadata for ${key}:`, error);
    return {};
  }
}

/**
 * List all firmware versions for a project
 */
async function listFirmwareVersions(bucket: string, projectName: string): Promise<FirmwareVersion[]> {
  const versions: FirmwareVersion[] = [];
  let continuationToken: string | undefined;

  do {
    const command = new ListObjectsV2Command({
      Bucket: bucket,
      Prefix: `${projectName}/`,
      ContinuationToken: continuationToken,
    });

    const response = await s3Client.send(command);

    if (response.Contents) {
      for (const object of response.Contents) {
        if (object.Key && object.Key.endsWith('/firmware.bin')) {
          try {
            const version = extractVersion(object.Key);
            // Get metadata to check if internal
            const metadata = await getFirmwareMetadata(bucket, object.Key);

            versions.push({
              version,
              key: object.Key,
              size: object.Size || 0,
              lastModified: object.LastModified || new Date(),
              internal: metadata.internal,
            });
          } catch (error) {
            console.warn(`Skipping invalid key: ${object.Key}`, error);
          }
        }
      }
    }

    continuationToken = response.NextContinuationToken;
  } while (continuationToken);

  return versions;
}

/**
 * Delete old firmware versions
 */
async function deleteFirmwareVersions(bucket: string, versions: FirmwareVersion[]): Promise<void> {
  console.log(`Deleting ${versions.length} old firmware versions`);

  for (const version of versions) {
    try {
      // Delete the firmware.bin file
      await s3Client.send(new DeleteObjectCommand({
        Bucket: bucket,
        Key: version.key,
      }));
      console.log(`✅ Deleted: ${version.key}`);

      // Also try to delete the version folder if it's empty
      // (S3 doesn't have folders, but some tools create 0-byte objects)
      const folderKey = version.key.substring(0, version.key.lastIndexOf('/') + 1);
      try {
        await s3Client.send(new DeleteObjectCommand({
          Bucket: bucket,
          Key: folderKey,
        }));
        console.log(`✅ Deleted folder marker: ${folderKey}`);
      } catch (error) {
        // Ignore errors deleting folder markers (they might not exist)
      }
    } catch (error) {
      console.error(`❌ Failed to delete ${version.key}:`, error);
    }
  }
}

/**
 * Build and upload manifest.json for the project
 */
async function updateManifest(bucket: string, projectName: string, versions: FirmwareVersion[]): Promise<void> {
  console.log(`Updating manifest for ${projectName} with ${versions.length} versions`);

  const manifestVersions: ManifestVersion[] = versions.map(v => ({
    version: v.version,
    timestamp: v.lastModified.toISOString(),
    firmware_path: v.key,
    size: v.size,
  }));

  // Check if any version is marked as internal (project-level flag)
  const isInternal = versions.some(v => v.internal === true);

  const manifest: Manifest = {
    project: projectName,
    versions: manifestVersions,
    ...(isInternal && { internal: true }),  // Only include if true
  };

  const manifestKey = `${projectName}/manifest.json`;

  await s3Client.send(new PutObjectCommand({
    Bucket: bucket,
    Key: manifestKey,
    Body: JSON.stringify(manifest, null, 2),
    ContentType: 'application/json',
  }));

  console.log(`✅ Updated manifest: ${manifestKey}${isInternal ? ' (internal)' : ''}`);
}

/**
 * Process a single S3 event record
 */
async function processRecord(record: S3EventRecord): Promise<void> {
  const bucket = record.s3.bucket.name;
  const key = decodeURIComponent(record.s3.object.key.replace(/\+/g, ' '));

  console.log(`Processing: s3://${bucket}/${key}`);

  // Extract project name
  let projectName: string;
  try {
    projectName = extractProjectName(key);
  } catch (error) {
    console.warn(`Skipping non-firmware file: ${key}`);
    return;
  }

  console.log(`Project: ${projectName}`);

  // List all firmware versions for this project
  const allVersions = await listFirmwareVersions(bucket, projectName);
  console.log(`Found ${allVersions.length} total firmware versions`);

  if (allVersions.length === 0) {
    console.log('No firmware versions found');
    return;
  }

  // Sort versions (newest first)
  allVersions.sort((a, b) => compareVersions(b.version, a.version));

  console.log('Sorted versions:');
  allVersions.forEach((v, i) => {
    console.log(`  ${i + 1}. ${v.version} (${v.key})`);
  });

  // Keep top 3 versions
  const versionsToKeep = allVersions.slice(0, 3);
  const versionsToDelete = allVersions.slice(3);

  console.log(`Keeping ${versionsToKeep.length} versions, deleting ${versionsToDelete.length} versions`);

  // Delete old versions
  if (versionsToDelete.length > 0) {
    await deleteFirmwareVersions(bucket, versionsToDelete);
  }

  // Update manifest with retained versions
  await updateManifest(bucket, projectName, versionsToKeep);

  console.log(`✅ Completed processing for ${projectName}`);
}

/**
 * Lambda handler for S3 firmware upload events
 */
export async function handler(event: S3Event): Promise<void> {
  console.log('Firmware Cleanup Lambda triggered');
  console.log(`Processing ${event.Records.length} records`);

  for (const record of event.Records) {
    try {
      await processRecord(record);
    } catch (error) {
      console.error('Error processing record:', error);
      console.error('Record:', JSON.stringify(record, null, 2));
      // Continue processing other records
    }
  }

  console.log('✅ All records processed');
}
