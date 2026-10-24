// Finding programs, without Jest imports (also used by the global setup).

import { accessSync, constants, existsSync, readdirSync } from 'node:fs';
import path from 'node:path';

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
