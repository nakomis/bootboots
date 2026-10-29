import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { MODEL_FILE, modelFor, parseModelFile } from '../lib/model-config';

const good = { artefact: 'local-models/20260331T203825/model.tar.gz', label: '20260331T203825' };

const withFile = (json: unknown, fn: (file: string) => void) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'model-json-'));
  const file = path.join(dir, 'model.json');
  fs.writeFileSync(file, JSON.stringify(json));
  try {
    fn(file);
  } finally {
    fs.rmSync(dir, { recursive: true });
  }
};

test('the committed model.json is valid and gives sandbox a model', () => {
  expect(modelFor('sandbox', MODEL_FILE)).toEqual(expect.objectContaining({ label: expect.any(String) }));
  expect(() => modelFor('prod', MODEL_FILE)).not.toThrow();
});

test('accepts a model or null per environment', () => {
  expect(parseModelFile({ sandbox: good, prod: null })).toEqual({ sandbox: good, prod: null });
});

test.each([
  [{ sandbox: good }, /no "prod" entry/],
  [{ sandbox: { ...good, artefact: 's3://bucket/x.tar.gz' }, prod: null }, /artefact/],
  [{ sandbox: { ...good, artefact: 'model.zip' }, prod: null }, /artefact/],
  [{ sandbox: { ...good, label: 'has spaces' }, prod: null }, /label/],
  [{ sandbox: { ...good, label: 'x'.repeat(41) }, prod: null }, /label/],
  [{ sandbox: { artefact: good.artefact }, prod: null }, /label/],
  [{ sandbox: { ...good, label: '20991231T000000' }, prod: null }, /directory in its artefact path/],
  [{ sandbox: good, prod: null, staging: null }, /unknown environment/],
  [null, /must be an object/],
  [[good], /must be an object/],
])('rejects %j', (json, message) => {
  expect(() => parseModelFile(json)).toThrow(message);
});

test('sandbox without a model is refused (it would remove the live endpoint)', () => {
  withFile({ sandbox: null, prod: good }, (file) => {
    expect(() => modelFor('sandbox', file)).toThrow(/sandbox must name a model/);
    expect(modelFor('prod', file)).toEqual(good);
  });
});
