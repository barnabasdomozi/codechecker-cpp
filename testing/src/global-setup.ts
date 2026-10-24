// Runs once before all test files: checks the configuration and creates the
// session folder that the workers share.

import { mkdirSync, mkdtempSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { config, ENV_SESSION_DIR } from './config';
import { findOnPath } from './which';

export default async function globalSetup(): Promise<void> {
  // Fails early, with a clear message, if CODECHECKER_BIN is wrong.
  const cfg = config();
  // The documented results include Cppcheck (DOC_ANALYZERS), and naming an
  // analyzer that is not installed makes `analyze` fail.
  if (!findOnPath('cppcheck')) {
    throw new Error(
      'Cppcheck is not on PATH. The documented results (docs/ and testing/analyze-targets/) include Cppcheck 2.17.1 reports; install Cppcheck first.',
    );
  }
  const session = mkdtempSync(path.join(os.tmpdir(), 'ccbb-'));
  mkdirSync(path.join(session, 'f'), { recursive: true });
  mkdirSync(path.join(session, 'shared'), { recursive: true });
  // Worker processes are started after this and inherit the variable.
  process.env[ENV_SESSION_DIR] = session;
  process.stderr.write(
    `\nTesting ${cfg.codechecker}` +
      (cfg.compareCodechecker ? `\nComparing with ${cfg.compareCodechecker}` : '') +
      `\nSession folder: ${session}${cfg.keepTemp ? ' (kept)' : ''}\n`,
  );
}
