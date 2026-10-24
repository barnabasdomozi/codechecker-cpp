// Optional tools some use cases need. A test whose requirement is missing
// is skipped, and its name says why.

import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { toolPath } from './config';
import { findOnPath, postgresBinDir } from './which';

export { findOnPath };

export type Requirement =
  /** An executable on PATH, e.g. 'cppcheck'. */
  | `command:${string}`
  /** A standalone tool next to CodeChecker, e.g. 'tool:report-converter'. */
  | `tool:${string}`
  /** Clang has the statistics collector checkers (Ericsson's Clang fork). */
  | 'clang-statistics-checkers'
  /** Clang was built with the Z3 solver. */
  | 'clang-z3'
  /** PostgreSQL server binaries (initdb, pg_ctl), for a temporary server. */
  | 'postgresql-server';


const cache = new Map<Requirement, boolean>();

function clangHasStatisticsCheckers(): boolean {
  const clang = findOnPath('clang');
  if (!clang) return false;
  try {
    const out = execFileSync(clang, ['-cc1', '-analyzer-checker-help-developer'], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    return /statisticscollector/i.test(out);
  } catch {
    return false;
  }
}

function clangHasZ3(): boolean {
  const clang = findOnPath('clang');
  if (!clang) return false;
  try {
    const out = execFileSync(clang, ['-cc1', '-analyzer-constraints=z3', '-x', 'c', '/dev/null', '-analyze'], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    return !/not support|unsupported/i.test(out);
  } catch {
    return false;
  }
}

export function isAvailable(requirement: Requirement): boolean {
  const known = cache.get(requirement);
  if (known !== undefined) return known;
  let ok: boolean;
  if (requirement.startsWith('command:')) {
    ok = findOnPath(requirement.slice('command:'.length)) !== undefined;
  } else if (requirement.startsWith('tool:')) {
    ok = toolPath(requirement.slice('tool:'.length)) !== undefined;
  } else if (requirement === 'postgresql-server') {
    ok = postgresBinDir() !== undefined;
  } else if (requirement === 'clang-statistics-checkers') {
    ok = clangHasStatisticsCheckers();
  } else {
    ok = clangHasZ3();
  }
  cache.set(requirement, ok);
  return ok;
}

export function missingRequirements(requirements: Requirement[]): string[] {
  return requirements.filter((r) => !isAvailable(r)).map((r) => r.replace(/^(command|tool):/, ''));
}

/** True if a file exists; for readable conditions in tests. */
export function fileExists(p: string): boolean {
  return existsSync(p);
}
