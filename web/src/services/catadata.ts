import { ConditionalCheckFailedException } from '@aws-sdk/client-dynamodb';
import { ScanCommand, type ScanCommandOutput, UpdateCommand } from '@aws-sdk/lib-dynamodb';
import type { AwsServices } from './aws';
import type { CatadataRecord } from './types';

type Item = Record<string, unknown>;

/** Scans every page of a filtered scan. */
export async function scanAll(
  aws: AwsServices,
  input: Omit<ScanCommand['input'], 'TableName'>,
): Promise<Item[]> {
  const items: Item[] = [];
  let startKey: Record<string, unknown> | undefined;
  do {
    const page: ScanCommandOutput = await aws.ddb.send(
      new ScanCommand({ ...input, TableName: aws.config.catadataTable, ExclusiveStartKey: startKey }),
    );
    items.push(...((page.Items ?? []) as Item[]));
    startKey = page.LastEvaluatedKey;
  } while (startKey);
  return items;
}

const str = (v: unknown) => (typeof v === 'string' ? v : undefined);

export const toRecord = (item: Item): CatadataRecord | undefined => {
  const imageName = str(item.imageName);
  const uuid = str(item.uuid);
  if (!imageName || !uuid) return undefined;
  return { imageName, uuid, user: str(item.user), cat: str(item.cat), reviewedAt: str(item.reviewedAt) };
};

/** Records that still need a cat label. */
export async function getUnlabelledRecords(aws: AwsServices): Promise<CatadataRecord[]> {
  const items = await scanAll(aws, {
    FilterExpression: 'attribute_not_exists(#cat) OR #cat = :emptyString',
    ExpressionAttributeNames: { '#cat': 'cat' },
    ExpressionAttributeValues: { ':emptyString': '' },
  });
  return items.map(toRecord).filter((r): r is CatadataRecord => !!r);
}

/**
 * Claims the next unlabelled record for `user`, taking records from the end of `records` (which is
 * consumed). Records someone labelled in the meantime are skipped.
 */
export async function claimRecord(
  aws: AwsServices,
  records: CatadataRecord[],
  user: string,
  now: () => string = () => new Date().toISOString(),
): Promise<CatadataRecord | null> {
  for (let candidate = records.pop(); candidate; candidate = records.pop()) {
    const claimed = { ...candidate, user, claimedAt: now() };
    try {
      await aws.ddb.send(
        new UpdateCommand({
          TableName: aws.config.catadataTable,
          Key: { imageName: claimed.imageName, uuid: claimed.uuid },
          UpdateExpression: 'SET #user = :user, #claimedAt = :claimedAt',
          ConditionExpression: 'attribute_not_exists(#cat) OR #cat = :emptyString',
          ExpressionAttributeNames: { '#user': 'user', '#claimedAt': 'claimedAt', '#cat': 'cat' },
          ExpressionAttributeValues: { ':user': user, ':claimedAt': claimed.claimedAt, ':emptyString': '' },
        }),
      );
      return claimed;
    } catch (err) {
      if (err instanceof ConditionalCheckFailedException) continue;
      throw err;
    }
  }
  return null;
}

/** Saves the cat label for a claimed record. */
export async function labelRecord(
  aws: AwsServices,
  record: CatadataRecord,
  cat: string,
  now: () => string = () => new Date().toISOString(),
): Promise<void> {
  await aws.ddb.send(
    new UpdateCommand({
      TableName: aws.config.catadataTable,
      Key: { imageName: record.imageName, uuid: record.uuid },
      UpdateExpression: 'SET #cat = :cat, #reviewedAt = :reviewedAt, #claimedAt = :claimedAt, #user = :user',
      ExpressionAttributeNames: {
        '#cat': 'cat',
        '#user': 'user',
        '#claimedAt': 'claimedAt',
        '#reviewedAt': 'reviewedAt',
      },
      ExpressionAttributeValues: {
        ':cat': cat,
        ':user': record.user,
        ':claimedAt': record.claimedAt,
        ':reviewedAt': now(),
      },
    }),
  );
}
