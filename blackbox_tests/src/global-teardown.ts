// Runs once after all test files: removes the session folder.

import { rmSync } from 'node:fs';
import { ENV_KEEP, ENV_SESSION_DIR } from './config';

export default async function globalTeardown(): Promise<void> {
  const session = process.env[ENV_SESSION_DIR];
  if (session && process.env[ENV_KEEP] !== '1') {
    rmSync(session, { recursive: true, force: true });
  }
}
