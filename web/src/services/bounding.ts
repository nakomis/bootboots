import { ConditionalCheckFailedException } from '@aws-sdk/client-dynamodb';
import { UpdateCommand } from '@aws-sdk/lib-dynamodb';
import type { AwsServices } from './aws';
import { scanAll } from './catadata';
import type { BoundingBox, BoundingPoint, CatadataRecord } from './types';

export const BOUNDABLE_CATS = ['Boots', 'Chi', 'Kappa', 'Mu', 'Tau', 'NoCat'];

export type CatBoundingStats = { sorted: number; bounded: number };

/** Per-cat counts of labelled ("sorted") images and those with a bounding box. */
export async function getBoundingStats(aws: AwsServices): Promise<Record<string, CatBoundingStats>> {
  const stats: Record<string, CatBoundingStats> = {};
  for (const cat of BOUNDABLE_CATS) stats[cat] = { sorted: 0, bounded: 0 };

  const values = Object.fromEntries(BOUNDABLE_CATS.map((cat, i) => [`:c${i}`, cat]));
  const items = await scanAll(aws, {
    FilterExpression: `#cat IN (${Object.keys(values).join(',')})`,
    ExpressionAttributeNames: { '#cat': 'cat' },
    ExpressionAttributeValues: values,
    ProjectionExpression: '#cat, boundingBox',
  });
  for (const item of items) {
    const entry = typeof item.cat === 'string' ? stats[item.cat] : undefined;
    if (!entry) continue;
    entry.sorted++;
    if (item.boundingBox) entry.bounded++;
  }
  return stats;
}

/** Claims a random labelled-but-unbounded image of `cat` for `user`. */
export async function claimNextUnbounded(
  aws: AwsServices,
  cat: string,
  user: string,
  random: () => number = Math.random,
): Promise<CatadataRecord | null> {
  const items = await scanAll(aws, {
    FilterExpression: '#cat = :cat AND attribute_not_exists(boundingBox)',
    ExpressionAttributeNames: { '#cat': 'cat' },
    ExpressionAttributeValues: { ':cat': cat },
  });
  const candidates: CatadataRecord[] = items
    .filter((i) => typeof i.imageName === 'string' && typeof i.uuid === 'string')
    .map((i) => ({
      imageName: i.imageName as string,
      uuid: i.uuid as string,
      cat: i.cat as string | undefined,
      user: i.user as string | undefined,
    }));

  // Shuffle so several people bounding at once don't race for the same record.
  for (let i = candidates.length - 1; i > 0; i--) {
    const j = Math.floor(random() * (i + 1));
    [candidates[i], candidates[j]] = [candidates[j], candidates[i]];
  }

  for (const record of candidates) {
    const claimedAt = new Date().toISOString();
    try {
      await aws.ddb.send(
        new UpdateCommand({
          TableName: aws.config.catadataTable,
          Key: { imageName: record.imageName, uuid: record.uuid },
          UpdateExpression: 'SET #user = :user, #claimedAt = :claimedAt',
          ConditionExpression: 'attribute_not_exists(boundingBox)',
          ExpressionAttributeNames: { '#user': 'user', '#claimedAt': 'claimedAt' },
          ExpressionAttributeValues: { ':user': user, ':claimedAt': claimedAt },
        }),
      );
      return { ...record, user, claimedAt };
    } catch (err) {
      if (err instanceof ConditionalCheckFailedException) continue;
      throw err;
    }
  }
  return null;
}

export async function saveBoundingData(
  aws: AwsServices,
  record: CatadataRecord,
  boundingBox: BoundingBox,
  points: BoundingPoint[],
  user: string,
): Promise<void> {
  await aws.ddb.send(
    new UpdateCommand({
      TableName: aws.config.catadataTable,
      Key: { imageName: record.imageName, uuid: record.uuid },
      UpdateExpression: 'SET boundingBox = :bbox, boundingPoints = :pts, boundedAt = :at, boundedBy = :by',
      ExpressionAttributeValues: {
        ':bbox': boundingBox,
        ':pts': points,
        ':at': new Date().toISOString(),
        ':by': user,
      },
    }),
  );
}

/** Removes the cat label so the image goes back into the labelling queue. */
export async function unlabelRecord(aws: AwsServices, record: CatadataRecord): Promise<void> {
  await aws.ddb.send(
    new UpdateCommand({
      TableName: aws.config.catadataTable,
      Key: { imageName: record.imageName, uuid: record.uuid },
      UpdateExpression: 'REMOVE #cat',
      ExpressionAttributeNames: { '#cat': 'cat' },
    }),
  );
}
