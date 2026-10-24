// Every test file works in its own sandbox folder, with its own HOME and
// TMPDIR, so that files CodeChecker writes there (sessions, the list of
// running servers, temporary folders) never leak between test files.

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect } from '@jest/globals';
import { config, sessionDir } from './config';
import { setBaseEnv } from './run';

export interface Sandbox {
  /** The sandbox root. */
  readonly dir: string;
  /** HOME of every command run from this test file. */
  readonly home: string;
  /** TMPDIR of every command run from this test file. */
  readonly tmp: string;
  /** A new, empty folder inside the sandbox. */
  newDir(label: string): string;
  /** Writes a file inside the sandbox (or at an absolute path) and returns its path. */
  write(relativeOrAbsolute: string, content: string): string;
  /**
   * Runs `fn` at the end of the test file, before the sandbox folder is
   * removed (e.g. to stop servers whose data is in the sandbox). Callbacks
   * run in reverse order of registration.
   */
  onCleanup(fn: () => Promise<void> | void): void;
}

class SandboxImpl implements Sandbox {
  private counter = 0;
  readonly cleanups: Array<() => Promise<void> | void> = [];

  onCleanup(fn: () => Promise<void> | void): void {
    this.cleanups.push(fn);
  }
  constructor(
    readonly dir: string,
    readonly home: string,
    readonly tmp: string,
  ) {}

  newDir(label: string): string {
    this.counter += 1;
    const safe = label.replace(/[^\w.-]+/g, '_').slice(0, 40);
    const dir = path.join(this.dir, `${String(this.counter).padStart(3, '0')}-${safe}`);
    mkdirSync(dir, { recursive: true });
    return dir;
  }

  write(relativeOrAbsolute: string, content: string): string {
    const target = path.isAbsolute(relativeOrAbsolute)
      ? relativeOrAbsolute
      : path.join(this.dir, relativeOrAbsolute);
    mkdirSync(path.dirname(target), { recursive: true });
    writeFileSync(target, content);
    return target;
  }
}

let current: SandboxImpl | undefined;

/** Creates a sandbox folder (normally called through useSandbox). */
export function createSandbox(label: string): Sandbox {
  // Short names: Python's multiprocessing puts Unix sockets under TMPDIR,
  // and socket paths are limited to about 108 characters.
  const safe = label.replace(/[^\w.-]+/g, '_').slice(0, 16);
  const dir = mkdtempSync(path.join(sessionDir(), 'f', `${safe}-`));
  const home = path.join(dir, 'home');
  const tmp = path.join(dir, 't');
  mkdirSync(home, { recursive: true });
  mkdirSync(tmp, { recursive: true });
  current = new SandboxImpl(dir, home, tmp);
  setBaseEnv({
    HOME: home,
    TMPDIR: tmp,
    // Wide tables, so that they are not wrapped.
    COLUMNS: '250',
    // Never let a test talk to a real user's server by accident.
    CC_PASS_FILE: undefined,
    CC_SESSION_FILE: undefined,
    CC_PASSWORD: undefined,
  });
  return current;
}

/**
 * Registers the sandbox of the current test file. Call it once at the top of
 * a test file:
 *
 *     const box = useSandbox();
 *
 * The returned object can be used inside tests and hooks.
 */
export function useSandbox(): Sandbox {
  const proxy = new Proxy({} as Sandbox, {
    get(_target, property) {
      if (!current) throw new Error('The sandbox is used before beforeAll ran.');
      const value = (current as unknown as Record<string | symbol, unknown>)[property];
      return typeof value === 'function' ? value.bind(current) : value;
    },
  });
  beforeAll(() => {
    const testPath = expect.getState().testPath ?? 'tests';
    createSandbox(path.basename(testPath).replace(/\.test\.ts$/, ''));
  });
  afterAll(async () => {
    if (!current) return;
    for (const fn of [...current.cleanups].reverse()) {
      try {
        await fn();
      } catch (error) {
        process.stderr.write(`Cleanup failed: ${String(error)}\n`);
      }
    }
    if (!config().keepTemp) {
      rmSync(current.dir, { recursive: true, force: true });
    }
  });
  return proxy;
}
