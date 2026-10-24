// Results that several test files read, made only once per test session.
//
// Test files run in parallel worker processes. The first worker that asks
// for a key creates it; the others wait for it. Users of a shared result
// must treat it as read-only.

import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { sessionDir } from './config';

const WAIT_LIMIT_MS = 20 * 60_000;

/**
 * Returns the folder for `key`, calling `create(folder)` first if no worker
 * has created it yet. `create` gets an empty folder and must fill it.
 * The value it returns is saved and returned to every caller.
 */
export async function shared<T>(key: string, create: (dir: string) => Promise<T>): Promise<{ dir: string; value: T }> {
  const root = path.join(sessionDir(), 'shared');
  mkdirSync(root, { recursive: true });
  const safe = key.replace(/[^\w.-]+/g, '_');
  const dir = path.join(root, safe);
  const lock = path.join(root, `${safe}.lock`);
  const done = path.join(root, `${safe}.done.json`);
  const failed = path.join(root, `${safe}.error.txt`);

  let owner = false;
  try {
    mkdirSync(lock);
    owner = true;
  } catch {
    // Another worker creates it.
  }

  if (owner) {
    // Built in place: report folders contain absolute paths, so the folder
    // cannot be renamed afterwards. Readers wait for the 'done' marker.
    rmSync(dir, { recursive: true, force: true });
    mkdirSync(dir, { recursive: true });
    try {
      const value = await create(dir);
      writeFileSync(done, JSON.stringify({ value }));
      return { dir, value };
    } catch (error) {
      writeFileSync(failed, error instanceof Error ? (error.stack ?? error.message) : String(error));
      throw error;
    }
  }

  const start = Date.now();
  for (;;) {
    if (existsSync(done)) {
      return { dir, value: (JSON.parse(readFileSync(done, 'utf8')) as { value: T }).value };
    }
    if (existsSync(failed)) {
      throw new Error(`Creating the shared result '${key}' failed in another test file:\n${readFileSync(failed, 'utf8')}`);
    }
    if (Date.now() - start > WAIT_LIMIT_MS) {
      throw new Error(`Timed out waiting for the shared result '${key}'.`);
    }
    await sleep(250);
  }
}
