// docs/usage/server-database.md

import { setTimeout as sleep } from 'node:timers/promises';
import { describe, expect } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { logMessages } from '../../src/normalize';
import { Server } from '../../src/server';

const box = useSandbox();

/** The option is ignored and a normal server starts (known issue #1). */
async function startsNormally(args: string[]): Promise<void> {
  const server = await Server.start(box, { args });
  await server.waitForOutput('Server waiting for client requests on');
  await sleep(2000);
  expect(server.running).toBe(true);
  await server.stop();
}

describe('usage/server-database.md', () => {
  useCase('server-database.md', 'Automatic check at start', async () => {
    const server = await Server.start(box);
    await server.waitForOutput('Server waiting for client requests on');
    const info = logMessages(server.output, 'INFO');
    expect(info).toContain('Checking configuration database ...');
    // A new workspace: the databases are created first.
    expect(info).toContain('Database schema is missing.');
    await server.stop();

    // The next start finds the databases up to date.
    const again = await Server.start(box, { workspace: server.workspace });
    await again.waitForOutput('Server waiting for client requests on');
    expect(logMessages(again.output, 'INFO')).toContain('Database is up to date.');
    expect(again.output).toMatch(
      /│ Product endpoint +│ Database status +│ Database location +│ Schema version in the database +│ Schema version in the package +│/,
    );
    expect(again.output).toMatch(/│ Default +│ Database is up to date\. +│/);
    await again.stop();
  });

  for (const args of [['--db-status', 'all'], ['--db-status', 'Default']]) {
    useCase('server-database.md', 'Show the database status', () => startsNormally(args), { name: args.join(' '), knownIssue: 1 });
  }

  for (const args of [['--db-upgrade-schema', 'all'], ['--db-upgrade-schema', 'Default'], ['--db-force-upgrade']]) {
    useCase('server-database.md', 'Upgrade product databases', () => startsNormally(args), { name: args.join(' '), knownIssue: 1 });
  }

  useCase('server-database.md', 'Upgrade product databases', async () => {
    const result = await cc(['server', '-w', box.newDir('ws'), '--db-upgrade-schema', 'all', '--db-force-upgrade'], { cwd: box.dir });
    expect(result).toExitWith(1);
    expect(result.output).toContain('argument --db-force-upgrade: not allowed with argument --db-upgrade-schema');
  }, { name: 'the options exclude each other', knownIssue: 'H4' });
});
