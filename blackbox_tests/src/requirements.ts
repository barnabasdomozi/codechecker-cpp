// Optional tools some use cases need. A test whose requirement is missing
// is skipped, and its name says why. No Jest imports: the global setup uses
// this file too.

import { accessSync, constants, existsSync, readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { toolPath } from './config';

export type Requirement =
  /** An executable on PATH, e.g. 'cppcheck'. */
  | `command:${string}`
  /** A standalone tool next to CodeChecker, e.g. 'tool:report-converter'. */
  | `tool:${string}`
  /** Clang has the statistics collector checkers (Ericsson's Clang fork). */
  | 'clang-statistics-checkers'
  /** PostgreSQL server binaries (initdb, pg_ctl), for a temporary server. */
  | 'postgresql-server';

export function findOnPath(name: string): string | undefined {
  for (const dir of (process.env.PATH ?? '').split(path.delimiter)) {
    if (!dir) continue;
    const candidate = path.join(dir, name);
    try {
      accessSync(candidate, constants.X_OK);
      return candidate;
    } catch {
      // Not here.
    }
  }
  return undefined;
}

/** Folder with `initdb` and `pg_ctl`: PATH first, then /usr/lib/postgresql/<newest>/bin. */
export function postgresBinDir(): string | undefined {
  const initdb = findOnPath('initdb');
  if (initdb && findOnPath('pg_ctl')) return path.dirname(initdb);
  const root = '/usr/lib/postgresql';
  if (!existsSync(root)) return undefined;
  const versions = readdirSync(root)
    .filter((v) => existsSync(path.join(root, v, 'bin', 'initdb')) && existsSync(path.join(root, v, 'bin', 'pg_ctl')))
    .sort((a, b) => Number(b) - Number(a));
  return versions.length > 0 ? path.join(root, versions[0], 'bin') : undefined;
}

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

const cache = new Map<Requirement, boolean>();

export function isAvailable(requirement: Requirement): boolean {
  let ok = cache.get(requirement);
  if (ok !== undefined) return ok;
  if (requirement.startsWith('command:')) ok = findOnPath(requirement.slice('command:'.length)) !== undefined;
  else if (requirement.startsWith('tool:')) ok = toolPath(requirement.slice('tool:'.length)) !== undefined;
  else if (requirement === 'postgresql-server') ok = postgresBinDir() !== undefined;
  else ok = clangHasStatisticsCheckers();
  cache.set(requirement, ok);
  return ok;
}
