import { ConditionalCheckFailedException } from '@aws-sdk/client-dynamodb';
import { GetObjectCommand, S3Client } from '@aws-sdk/client-s3';
import { DynamoDBDocumentClient, ScanCommand, UpdateCommand } from '@aws-sdk/lib-dynamodb';
import { mockClient } from 'aws-sdk-client-mock';
import { config, credentials } from '../test/render';
import { createAwsServices } from './aws';
import { claimNextUnbounded, getBoundingStats, saveBoundingData, unlabelRecord } from './bounding';
import { claimRecord, getUnlabelledRecords, labelRecord, toRecord } from './catadata';
import { getEvents } from './events';
import { getTrainingImage, trainingImageKey } from './images';
import { blobToBase64, segment } from './sam';

const ddb = mockClient(DynamoDBDocumentClient);
const s3 = mockClient(S3Client);
const aws = createAwsServices(config, credentials);
const conflict = () => new ConditionalCheckFailedException({ message: 'taken', $metadata: {} });

beforeEach(() => {
  ddb.reset();
  s3.reset();
});

test('getUnlabelledRecords pages through the scan and drops malformed rows', async () => {
  ddb
    .on(ScanCommand)
    .resolvesOnce({
      Items: [{ imageName: 'a.jpg', uuid: '1' }, { imageName: 'x' }],
      LastEvaluatedKey: { k: 1 },
    })
    .resolvesOnce({ Items: [{ imageName: 'b.jpg', uuid: '2', cat: '' }] });
  const records = await getUnlabelledRecords(aws);
  expect(records.map((r) => r.imageName)).toEqual(['a.jpg', 'b.jpg']);
  const calls = ddb.commandCalls(ScanCommand);
  expect(calls[0].args[0].input.TableName).toBe('catadata');
  expect(calls[1].args[0].input.ExclusiveStartKey).toEqual({ k: 1 });
  expect(toRecord({ imageName: 1, uuid: 'x' })).toBeUndefined();
});

test('claimRecord skips records someone else labelled', async () => {
  ddb.on(UpdateCommand).rejectsOnce(conflict()).resolves({});
  const queue = [
    { imageName: 'a', uuid: '1' },
    { imageName: 'b', uuid: '2' },
  ];
  const claimed = await claimRecord(aws, queue, 'martin', () => 'now');
  expect(claimed).toEqual({ imageName: 'a', uuid: '1', user: 'martin', claimedAt: 'now' });
  expect(queue).toEqual([]);
  expect(await claimRecord(aws, [], 'martin')).toBeNull();
  ddb.on(UpdateCommand).rejects(new Error('boom'));
  await expect(claimRecord(aws, [{ imageName: 'c', uuid: '3' }], 'martin')).rejects.toThrow('boom');
});

test('labelRecord saves the cat and review time', async () => {
  ddb.on(UpdateCommand).resolves({});
  await labelRecord(aws, { imageName: 'a', uuid: '1', user: 'martin', claimedAt: 't0' }, 'Boots', () => 't1');
  expect(ddb.commandCalls(UpdateCommand)[0].args[0].input.ExpressionAttributeValues).toEqual({
    ':cat': 'Boots',
    ':user': 'martin',
    ':claimedAt': 't0',
    ':reviewedAt': 't1',
  });
});

test('bounding stats count sorted and bounded images per cat', async () => {
  ddb.on(ScanCommand).resolves({
    Items: [{ cat: 'Boots', boundingBox: {} }, { cat: 'Boots' }, { cat: 'Mu' }, { cat: 'Unknown' }, {}],
  });
  const stats = await getBoundingStats(aws);
  expect(stats.Boots).toEqual({ sorted: 2, bounded: 1 });
  expect(stats.Mu).toEqual({ sorted: 1, bounded: 0 });
  expect(stats.NoCat).toEqual({ sorted: 0, bounded: 0 });
});

test('claimNextUnbounded claims the first free candidate', async () => {
  ddb.on(ScanCommand).resolves({
    Items: [
      { imageName: 'a', uuid: '1', cat: 'Boots' },
      { imageName: 'b', uuid: '2', cat: 'Boots' },
      { bad: true },
    ],
  });
  ddb.on(UpdateCommand).rejectsOnce(conflict()).resolves({});
  const claimed = await claimNextUnbounded(aws, 'Boots', 'martin', () => 0.99);
  expect(claimed).toMatchObject({ user: 'martin', cat: 'Boots' });
  expect(ddb.commandCalls(UpdateCommand)).toHaveLength(2);

  ddb.on(ScanCommand).resolves({ Items: [] });
  expect(await claimNextUnbounded(aws, 'Mu', 'martin')).toBeNull();

  ddb.on(ScanCommand).resolves({ Items: [{ imageName: 'a', uuid: '1' }] });
  ddb.on(UpdateCommand).rejects(conflict());
  expect(await claimNextUnbounded(aws, 'Mu', 'martin')).toBeNull();
  ddb.on(UpdateCommand).rejects(new Error('denied'));
  await expect(claimNextUnbounded(aws, 'Mu', 'martin')).rejects.toThrow('denied');
});

test('saveBoundingData and unlabelRecord update the record', async () => {
  ddb.on(UpdateCommand).resolves({});
  const record = { imageName: 'a', uuid: '1' };
  await saveBoundingData(
    aws,
    record,
    { xmin: 1, ymin: 2, xmax: 3, ymax: 4 },
    [{ x: 1, y: 1, label: 1 }],
    'martin',
  );
  await unlabelRecord(aws, record);
  const [save, unlabel] = ddb.commandCalls(UpdateCommand).map((c) => c.args[0].input);
  expect(save.ExpressionAttributeValues?.[':by']).toBe('martin');
  expect(unlabel.UpdateExpression).toBe('REMOVE #cat');
});

test('training images come from catcam-training/', async () => {
  expect(trainingImageKey('catcam-training/x.jpg')).toBe('catcam-training/x.jpg');
  s3.on(GetObjectCommand).resolves({
    Body: { transformToByteArray: async () => new Uint8Array([1, 2, 3]) } as never,
    ContentType: 'image/jpeg',
  });
  const blob = await getTrainingImage(aws, { imageName: 'x.jpg', uuid: '1' });
  expect(blob.size).toBe(3);
  expect(s3.commandCalls(GetObjectCommand)[0].args[0].input).toEqual({
    Bucket: 'bootboots-images-975050268859-eu-west-2',
    Key: 'catcam-training/x.jpg',
  });
  s3.on(GetObjectCommand).resolves({});
  await expect(getTrainingImage(aws, { imageName: 'y', uuid: '2' })).rejects.toThrow('No image data');
});

test('getEvents signs the request and reports API errors', async () => {
  const fetchImpl = vi.fn(
    async (_url: string, _init?: RequestInit) => new Response(JSON.stringify([{ id: 'e1' }])),
  );
  expect(await getEvents(aws, 0.7, fetchImpl as typeof fetch)).toEqual([{ id: 'e1' }]);
  const [url, init] = fetchImpl.mock.calls[0];
  expect(url).toBe('https://api.bootboots.sandbox.nakomis.com/events?minConfidence=0.7');
  const headers = init?.headers as Record<string, string>;
  expect(headers.authorization).toMatch(/^AWS4-HMAC-SHA256 Credential=AKID\/\d{8}\/eu-west-2\/execute-api/);
  expect(headers['x-amz-security-token']).toBe('token');
  expect(headers.host).toBeUndefined();

  const failing = vi.fn(async () => new Response('nope', { status: 403, statusText: 'Forbidden' }));
  await expect(getEvents(aws, 0.5, failing as typeof fetch)).rejects.toThrow(
    'Events API error: 403 Forbidden - nope',
  );
});

test('segment posts the image and points to the SAM server', async () => {
  const blob = new Blob([new Uint8Array([104, 105])]);
  expect(await blobToBase64(blob)).toBe('aGk=');
  const fetchImpl = vi.fn(async () => new Response(JSON.stringify({ overlay: 'o', bounding_box: {} })));
  const out = await segment(
    'https://sam',
    blob,
    [{ x: 1, y: 2, label: 1 }],
    10,
    20,
    fetchImpl as typeof fetch,
  );
  expect(out.overlay).toBe('o');
  const [url, init] = fetchImpl.mock.calls[0] as unknown as [string, RequestInit];
  expect(url).toBe('https://sam/segment');
  expect(JSON.parse(init.body as string)).toEqual({
    image: 'aGk=',
    points: [{ x: 1, y: 2, label: 1 }],
    display_width: 10,
    display_height: 20,
  });
  const failing = vi.fn(async () => new Response('', { status: 500 }));
  await expect(segment('https://sam', blob, [], 1, 1, failing as typeof fetch)).rejects.toThrow(
    'SAM server error: 500',
  );
});
