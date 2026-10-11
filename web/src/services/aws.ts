import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { S3Client } from '@aws-sdk/client-s3';
import { DynamoDBDocumentClient } from '@aws-sdk/lib-dynamodb';
import type { AwsCredentialIdentityProvider } from '@smithy/types';
import type { BootbootsConfig } from '../config';

/** The AWS clients and settings the data services share. */
export interface AwsServices {
  config: BootbootsConfig;
  credentials: AwsCredentialIdentityProvider;
  ddb: DynamoDBDocumentClient;
  s3: S3Client;
}

export function createAwsServices(
  config: BootbootsConfig,
  credentials: AwsCredentialIdentityProvider,
): AwsServices {
  const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({ region: config.region, credentials }), {
    marshallOptions: { removeUndefinedValues: true },
  });
  const s3 = new S3Client({ region: config.region, credentials });
  return { config, credentials, ddb, s3 };
}
