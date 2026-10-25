// Reads the suite configuration from the environment.
//
// The suite is only told where the build under test is. It never asks which
// implementation (Python or C++) it is testing.

import { accessSync, constants, existsSync, statSync } from 'node:fs';
import path from 'node:path';

interface SuiteConfig {
  /** Absolute path of the `CodeChecker` executable under test. */
  codechecker: string;
  /** Folder that holds `CodeChecker` and the standalone tools. */
  binDir: string;
  /** Optional second build, for the comparison tests. */
  compareCodechecker?: string;
  /** `blackbox_tests/analyze-targets`. */
  projectsDir: string;
  /** `docs/` of the repository. */
  docsDir: string;
  /** Keep temporary folders after the run (`CODECHECKER_TEST_KEEP=1`). */
  keepTemp: boolean;
}

const ENV_BIN = 'CODECHECKER_BIN';
const ENV_COMPARE_BIN = 'CODECHECKER_COMPARE_BIN';
export const ENV_KEEP = 'CODECHECKER_TEST_KEEP';
/** Set by the global setup: the folder for this test session. */
export const ENV_SESSION_DIR = 'CODECHECKER_TEST_SESSION_DIR';

/** Checks that a path is an absolute path to an executable file. */
function checkExecutable(variable: string, value: string | undefined): string {
  if (!value) {
    throw new Error(
      `${variable} is not set. Set it to the absolute path of the CodeChecker ` +
        `executable to test, e.g. ${variable}=/path/to/CodeChecker/bin/CodeChecker`,
    );
  }
  if (!path.isAbsolute(value)) {
    throw new Error(`${variable} must be an absolute path, got '${value}'.`);
  }
  if (!existsSync(value) || !statSync(value).isFile()) {
    throw new Error(`${variable} points to '${value}', which is not a file.`);
  }
  try {
    accessSync(value, constants.X_OK);
  } catch {
    throw new Error(`${variable} points to '${value}', which is not executable.`);
  }
  return value;
}

let cached: SuiteConfig | undefined;

export function config(): SuiteConfig {
  if (cached) return cached;
  const codechecker = checkExecutable(ENV_BIN, process.env[ENV_BIN]);
  const compare = process.env[ENV_COMPARE_BIN]
    ? checkExecutable(ENV_COMPARE_BIN, process.env[ENV_COMPARE_BIN])
    : undefined;
  const testingDir = path.resolve(__dirname, '..');
  cached = {
    codechecker,
    binDir: path.dirname(codechecker),
    compareCodechecker: compare,
    projectsDir: path.join(testingDir, 'analyze-targets'),
    docsDir: path.resolve(testingDir, '..', 'docs'),
    keepTemp: process.env[ENV_KEEP] === '1',
  };
  return cached;
}

/** The session folder created by the global setup. */
export function sessionDir(): string {
  const dir = process.env[ENV_SESSION_DIR];
  if (!dir) throw new Error(`${ENV_SESSION_DIR} is not set; was the global setup run?`);
  return dir;
}

/**
 * Path of a standalone tool shipped next to `CodeChecker`, e.g.
 * `report-converter`. Returns undefined if the build has no such file.
 */
export function toolPath(name: string, codechecker = config().codechecker): string | undefined {
  const candidate = path.join(path.dirname(codechecker), name);
  try {
    accessSync(candidate, constants.X_OK);
    return candidate;
  } catch {
    return undefined;
  }
}
