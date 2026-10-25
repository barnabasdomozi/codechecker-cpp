// A temporary PostgreSQL server for the PostgreSQL use cases.
//
// A new cluster is created with `initdb` in the test file's sandbox and
// started with `pg_ctl` on a free port. It listens on localhost (password
// login, scram-sha-256) and on a Unix socket in the sandbox (no password,
// used by the tests to create roles and databases). It is stopped and
// removed at the end. An existing PostgreSQL server on the machine is never
// used or touched.

import { chmodSync, existsSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll } from '@jest/globals';
import { describeResult, run } from './run';
import { postgresBinDir } from './requirements';
import type { Sandbox } from './sandbox';
import { freePort } from './server';

export class TempPostgres {
  private constructor(
    readonly binDir: string,
    readonly dataDir: string,
    readonly socketDir: string,
    readonly port: number,
    readonly logFile: string,
  ) {}

  /** Creates and starts a cluster in a new sandbox folder. */
  static async start(box: Sandbox): Promise<TempPostgres> {
    const binDir = postgresBinDir();
    if (!binDir) throw new Error('PostgreSQL server binaries (initdb, pg_ctl) were not found.');
    const dir = box.newDir('pg');
    const dataDir = path.join(dir, 'data');
    const logFile = path.join(dir, 'postgres.log');
    const pwfile = box.write(path.join(dir, 'superuser-password'), 'superuser-password\n');
    const init = await run(
      path.join(binDir, 'initdb'),
      ['-D', dataDir, '-U', 'postgres', `--pwfile=${pwfile}`, '--auth-local=trust', '--auth-host=scram-sha-256', '-E', 'UTF8', '--locale=C'],
      { cwd: dir },
    );
    if (init.exitCode !== 0) throw new Error(`initdb failed:\n${describeResult(init)}`);
    const port = await freePort();
    const start = await run(
      path.join(binDir, 'pg_ctl'),
      ['-D', dataDir, '-l', logFile, '-w', '-t', '60', '-o', `-p ${port} -k ${dir} -c listen_addresses=localhost`, 'start'],
      { cwd: dir },
    );
    if (start.exitCode !== 0) throw new Error(`pg_ctl start failed:\n${describeResult(start)}`);
    return new TempPostgres(binDir, dataDir, dir, port, logFile);
  }

  /** Runs SQL as the superuser through the Unix socket. */
  async sql(statement: string, database = 'postgres'): Promise<string> {
    const psql = existsSync(path.join(this.binDir, 'psql')) ? path.join(this.binDir, 'psql') : 'psql';
    const result = await run(psql, ['-h', this.socketDir, '-p', String(this.port), '-U', 'postgres', '-d', database, '-v', 'ON_ERROR_STOP=1', '-Atc', statement], {
      cwd: this.socketDir,
    });
    if (result.exitCode !== 0) throw new Error(`psql failed:\n${describeResult(result)}`);
    return result.stdout.trim();
  }

  /** Creates a login role with a password, and databases owned by it. */
  async createUser(name: string, password: string, databases: string[] = []): Promise<void> {
    await this.sql(`CREATE ROLE "${name}" LOGIN PASSWORD '${password}';`);
    for (const db of databases) await this.sql(`CREATE DATABASE "${db}" OWNER "${name}";`);
  }

  /** Writes a libpq password file (`host:port:db:user:password` lines). */
  writePgpass(file: string, entries: Array<{ user: string; password: string }>): string {
    writeFileSync(file, entries.map((e) => `localhost:${this.port}:*:${e.user}:${e.password}\n`).join(''));
    chmodSync(file, 0o600);
    return file;
  }

  async stop(): Promise<void> {
    await run(path.join(this.binDir, 'pg_ctl'), ['-D', this.dataDir, '-m', 'immediate', '-w', 'stop'], { cwd: this.socketDir });
  }
}

/** Starts a temporary PostgreSQL for the current test file. */
export function useTempPostgres(box: Sandbox): () => TempPostgres {
  let pg: TempPostgres | undefined;
  beforeAll(async () => {
    if (!postgresBinDir()) return;
    pg = await TempPostgres.start(box);
    // Stop it before the sandbox (with the data folder) is removed.
    box.onCleanup(() => pg?.stop());
  });
  return () => {
    if (!pg) throw new Error('The temporary PostgreSQL server is not running.');
    return pg;
  };
}
