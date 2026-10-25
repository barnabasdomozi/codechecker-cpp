// Every test file works in its own sandbox folder, with its own HOME and
// TMPDIR, so that files CodeChecker writes there (sessions, the list of
// running servers, temporary folders) never leak between test files.

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect } from '@jest/globals';
import { config, sessionDir } from './config';
import { setBaseEnv } from './run';

export class Sandbox {
  /** The sandbox root. Set in beforeAll. */
  dir = '';
  /** HOME of every command run from this test file. */
  home = '';
  /** TMPDIR of every command run from this test file. */
  tmp = '';
  private counter = 0;
  private readonly cleanups: Array<() => Promise<unknown> | void> = [];

  /** A new, empty folder inside the sandbox. */
  newDir(label: string): string {
    this.counter += 1;
    const safe = label.replace(/[^\w.-]+/g, '_').slice(0, 40);
    const dir = path.join(this.dir, `${String(this.counter).padStart(3, '0')}-${safe}`);
    mkdirSync(dir, { recursive: true });
    return dir;
  }

  /** Writes a file inside the sandbox (or at an absolute path) and returns its path. */
  write(relativeOrAbsolute: string, content: string): string {
    const target = path.resolve(this.dir, relativeOrAbsolute);
    mkdirSync(path.dirname(target), { recursive: true });
    writeFileSync(target, content);
    return target;
  }

  /**
   * Runs `fn` at the end of the test file, before the sandbox folder is
   * removed (e.g. to stop servers whose data is in the sandbox). Callbacks
   * run in reverse order of registration.
   */
  onCleanup(fn: () => Promise<unknown> | void): void {
    this.cleanups.push(fn);
  }

  async cleanUp(): Promise<void> {
    for (const fn of this.cleanups.reverse()) {
      try {
        await fn();
      } catch (error) {
        process.stderr.write(`Cleanup failed: ${String(error)}\n`);
      }
    }
    if (this.dir && !config().keepTemp) rmSync(this.dir, { recursive: true, force: true });
  }
}

/**
 * The sandbox of the current test file. Call it once at the top of a test
 * file (`const box = useSandbox();`); its folders exist from beforeAll on.
 */
export function useSandbox(): Sandbox {
  const box = new Sandbox();
  beforeAll(() => {
    // Short names: Python's multiprocessing puts Unix sockets under TMPDIR,
    // and socket paths are limited to about 108 characters.
    const label = path.basename(expect.getState().testPath ?? 'tests').replace(/\.test\.ts$/, '');
    box.dir = mkdtempSync(path.join(sessionDir(), 'f', `${label.replace(/[^\w.-]+/g, '_').slice(0, 16)}-`));
    box.home = path.join(box.dir, 'home');
    box.tmp = path.join(box.dir, 't');
    mkdirSync(box.home);
    mkdirSync(box.tmp);
    setBaseEnv({
      HOME: box.home,
      TMPDIR: box.tmp,
      // Wide tables, so that they are not wrapped.
      COLUMNS: '250',
      // Never let a test talk to a real user's server by accident.
      CC_PASS_FILE: undefined,
      CC_SESSION_FILE: undefined,
      CC_PASSWORD: undefined,
    });
  });
  afterAll(() => box.cleanUp());
  return box;
}
