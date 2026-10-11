import { GetObjectCommand } from '@aws-sdk/client-s3';
import type { AwsServices } from './aws';
import type { CatadataRecord } from './types';

const TRAINING_PREFIX = 'catcam-training/';

/** Training images live in the bootboots images bucket under catcam-training/. */
export const trainingImageKey = (imageName: string) =>
  imageName.startsWith(TRAINING_PREFIX) ? imageName : `${TRAINING_PREFIX}${imageName}`;

export async function getTrainingImage(aws: AwsServices, record: CatadataRecord): Promise<Blob> {
  const response = await aws.s3.send(
    new GetObjectCommand({ Bucket: aws.config.imagesBucket, Key: trainingImageKey(record.imageName) }),
  );
  const body = response.Body as { transformToByteArray?: () => Promise<Uint8Array> } | undefined;
  if (!body?.transformToByteArray) throw new Error(`No image data for ${record.imageName}`);
  const bytes = await body.transformToByteArray();
  return new Blob([bytes as BlobPart], { type: response.ContentType ?? 'image/jpeg' });
}
