// Post-build check: src/config.ts resolves '../config.json' against its own chunk, so the chunk that holds
// it must be in dist/assets/ with remoteEntry.js one level up (see the note in vite.config.ts).
import { existsSync, readdirSync, readFileSync } from 'node:fs';

const needle = '`../config.json`';
const containing = (dir) =>
  readdirSync(dir).filter((f) => f.endsWith('.js') && readFileSync(`${dir}/${f}`, 'utf8').includes(needle));

if (!existsSync('dist/remoteEntry.js')) throw new Error('dist/remoteEntry.js is missing');
const holders = containing('dist/assets');
if (holders.length === 0) {
  throw new Error('No chunk in dist/assets/ holds the ../config.json lookup: the config.ts layout assumption broke');
}
const stray = containing('dist');
if (stray.length > 0) throw new Error(`The config lookup ended up beside remoteEntry.js: ${stray.join(', ')}`);
console.log(`check-dist: config.json lookup is in assets/ (${holders.join(', ')})`);
