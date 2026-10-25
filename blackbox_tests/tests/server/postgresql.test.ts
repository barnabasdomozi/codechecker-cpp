// The PostgreSQL use cases of docs/usage/server.md and cmd-products.md, and
// PGPASSFILE (environment-variables.md), against a temporary PostgreSQL
// server (src/postgres.ts).

import { existsSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { logMessages } from '../../src/normalize';
import { sharedAnalysis } from '../../src/projects';
import { useTempPostgres } from '../../src/postgres';
import { isAvailable } from '../../src/requirements';
import { freePort, results, Server, store } from '../../src/server';

const box = useSandbox();
const pg = useTempPostgres(box);
const NEEDS = { requires: ['postgresql-server' as const] };

let pgpass = '';

beforeAll(async () => {
  if (!isAvailable('postgresql-server')) return;
  // The server's configuration database, as in the docs: user
  // `codechecker`, database `config`. The password comes from a pgpass file.
  await pg().createUser('codechecker', 'cc-config-pw', ['config']);
  pgpass = pg().writePgpass(path.join(box.dir, 'pgpass'), [{ user: 'codechecker', password: 'cc-config-pw' }]);
});

const postgresArgs = (database = 'config') => ['--postgresql', '--db-host', 'localhost', '--db-port', String(pg().port), '--db-username', 'codechecker', '--db-name', database];

let servers = 0;

/** A server whose configuration database is a new PostgreSQL database. */
async function withPostgresServer(body: (server: Server, configDb: string) => Promise<void>): Promise<void> {
  servers += 1;
  const configDb = servers === 1 ? 'config' : `config_${servers}`;
  if (configDb !== 'config') await pg().sql(`CREATE DATABASE "${configDb}" OWNER codechecker;`);
  const server = await Server.start(box, { args: postgresArgs(configDb), env: { PGPASSFILE: pgpass } });
  try {
    await body(server, configDb);
  } finally {
    await server.stop();
  }
}

describe('usage/server.md — PostgreSQL', () => {
  useCase('server.md', 'PostgreSQL configuration database', async () => {
    await withPostgresServer(async (server, configDb) => {
      const info = logMessages(server.output, 'INFO');
      expect(info).toContain('Checking configuration database ...');
      // The configuration database is in PostgreSQL, not in the workspace.
      expect(Number(await pg().sql("SELECT count(*) FROM information_schema.tables WHERE table_name = 'products';", configDb))).toBe(1);
      expect(existsSync(path.join(server.workspace, 'config.sqlite'))).toBe(false);

      // Unlike with SQLite, no Default product is created.
      const list = await server.cc(['cmd', 'products', 'list', '-o', 'json', '--url', server.serverUrl]);
      expect(list).toExitWith(0);
      expect(JSON.parse(list.stdout.slice(list.stdout.indexOf('[')))).toEqual([]);

      // Products added later work as usual.
      expect(await server.cc(['cmd', 'products', 'add', 'Default', '--url', server.serverUrl])).toExitWith(0);
      const demo = await sharedAnalysis('demo');
      await store(server, [demo.reports], 'pg_config_run');
      expect((await results(server, ['pg_config_run'])).length).toBe(6);
        expect(await pg().sql('SELECT endpoint FROM products;', configDb)).toBe('Default');
    });
  }, NEEDS);

  useCase('server.md', 'PostgreSQL configuration database', async () => {
    // The password file decides: with a wrong password the server does not start.
    const bad = pg().writePgpass(path.join(box.dir, 'bad-pgpass'), [{ user: 'codechecker', password: 'wrong' }]);
    const result = await cc(['server', '-w', path.join(box.newDir('bad'), 'ws'), '-v', String(await freePort()), ...postgresArgs()], {
      cwd: box.dir,
      env: { PGPASSFILE: bad },
      timeoutMs: 120_000,
    });
    expect(result).toExitWith(1);
    const errors = logMessages(result.output, 'ERROR');
    expect(errors.some((e) => new RegExp(`^Database connection failed to 'postgresql\\S*://codechecker@localhost:${pg().port}/config`).test(e))).toBe(true);
    expect(errors).toContain('Please check your command arguments.');
  }, { ...NEEDS, name: 'a wrong password in PGPASSFILE' });

  useCase('server.md', 'PostgreSQL configuration database', async () => {
    // The PostgreSQL options are rejected without --postgresql.
    const result = await cc(['server', '-w', path.join(box.newDir('nopg'), 'ws'), '--db-port', String(pg().port), '--db-name', 'config'], {
      cwd: box.dir,
      timeoutMs: 60_000,
    });
    expect(result).toExitWith(1);
    expect(result.output).toContain('argument --db-port: not allowed without argument --postgresql');
  }, { ...NEEDS, name: 'PostgreSQL options without --postgresql are rejected' });
});

describe('usage/cmd-products.md — PostgreSQL', () => {
  useCase('cmd-products.md', 'Add a product (PostgreSQL)', async () => {
    // The database must exist; the server connects to it.
    await pg().createUser('cc', 'secret', ['proj4']);
    await withPostgresServer(async (server) => {
      const added = await server.cc([
        'cmd', 'products', 'add', 'proj4', '--postgresql',
        '--db-host', 'localhost', '--db-port', String(pg().port),
        '--db-username', 'cc', '--db-password', 'secret', '--db-name', 'proj4',
        '--url', server.serverUrl,
      ]);
      expect(added).toExitWith(0);
      expect(logMessages(added.output, 'INFO')).toContain('Product added successfully.');
      const list = await server.cc(['cmd', 'products', 'list', '--url', server.serverUrl]);
      expect(list.stdout).toMatch(/│ Database is up to date\. +│ proj4 +│ proj4 +│/);

      const demo = await sharedAnalysis('demo');
      await store(server, [demo.reports], 'pgrun', [], 'proj4');
      expect((await results(server, ['pgrun'], [], 'proj4')).map((r) => r.bugHash).sort()).toEqual([
        '04d39a256ef58c412c5acf3f9280fe2e',
        '09296549497056a329a9ce22dda7d710',
        '249a121710b6a5cb467697f323de0d5f',
        '2e5ec744a6a360431ece6fe858d3f172',
        '3b2c4905253f6d0f7409d271d65dbdd6',
        '990560afd081e416b9c7cc4c772c4cff',
      ]);
      // The reports are in the PostgreSQL database.
      expect(Number(await pg().sql('SELECT count(*) FROM reports;', 'proj4'))).toBe(9);
    });
  }, NEEDS);

  useCase('cmd-products.md', 'Add a product (PostgreSQL)', async () => {
    // Defaults: the user name and the database name are the endpoint.
    await pg().createUser('pgdefaults', 'defaults-pw', ['pgdefaults']);
    await withPostgresServer(async (server) => {
      const added = await server.cc([
        'cmd', 'products', 'add', 'pgdefaults', '--postgresql',
        '--db-host', 'localhost', '--db-port', String(pg().port), '--db-password', 'defaults-pw',
        '--url', server.serverUrl,
      ]);
      expect(added).toExitWith(0);
      const demo = await sharedAnalysis('demo');
      await store(server, [demo.reports], 'defaults_run', [], 'pgdefaults');
      expect(Number(await pg().sql('SELECT count(*) FROM reports;', 'pgdefaults'))).toBe(9);
    });
  }, { ...NEEDS, name: 'user and database name default to the endpoint' });

  useCase('cmd-products.md', 'Add a product (PostgreSQL)', async () => {
    await withPostgresServer(async (server) => {
      const missing = await server.cc([
        'cmd', 'products', 'add', 'nodb', '--postgresql',
        '--db-host', 'localhost', '--db-port', String(pg().port),
        '--db-username', 'codechecker', '--db-password', 'cc-config-pw', '--db-name', 'no_such_database',
        '--url', server.serverUrl,
      ]);
      expect(missing).toExitWith(1);
      expect(logMessages(missing.output, 'ERROR')).toEqual(expect.arrayContaining(['Calling API endpoint: addProduct', 'Database error on server']));
    });
  }, { ...NEEDS, name: 'the database must exist' });
});
