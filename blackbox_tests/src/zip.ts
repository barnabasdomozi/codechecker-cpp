// Reading zip files with `unzip`. Tests that use these helpers need
// `requires: ['command:unzip']`.

import { execFileSync } from 'node:child_process';

const MAX_BUFFER = 256 * 1024 * 1024;

/** Names of the files (not folders) in a zip. */
export function zipNames(file: string): string[] {
  return execFileSync('unzip', ['-Z1', file], { encoding: 'utf8', maxBuffer: MAX_BUFFER })
    .split('\n')
    .filter((n) => n !== '' && !n.endsWith('/'))
    .sort();
}

/** The uncompressed content of one entry of a zip. */
export function zipEntry(file: string, name: string): Buffer {
  if (!zipNames(file).includes(name)) throw new Error(`${file} has no entry ${name}`);
  // `unzip` reads entry names as wildcard patterns; escape the special characters.
  const pattern = name.replace(/[[\]*?\\]/g, '\\$&');
  return execFileSync('unzip', ['-p', file, pattern], { maxBuffer: MAX_BUFFER });
}
