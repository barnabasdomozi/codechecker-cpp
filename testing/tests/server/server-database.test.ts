// docs/usage/server-database.md

import { setTimeout as sleep } from 'node:timers/promises';
import { describe, expect } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { logMessages } from '../../src/normalize';
import { Server } from '../../src/server';

const box = useSandbox();

async function waitForOutput(server: Server, text: string): Promise<void> {
  const deadline = Date.now() + 30_000;
  while (!server.output.includes(text)) {
    if (Date.now() > deadline) throw new Error(`'${text}' did not appear:\n${server.output}`);
    await sleep(200);
  }
}

describe('usage/server-database.md', () => {
  useCase('server-database.md', 'Automatic check at start', async () => {
    const server = await Server.start(box);
    try {
      await waitForOutput(server, 'Server waiting for client requests on');
      const info = logMessages(server.output, 'INFO');
      expect(info).toContain('Checking configuration database ...');
      // A new workspace: the databases are created first.
      expect(info).toContain('Database schema is missing.');
    } finally {
      await server.stop();
    }
    // The next start finds the databases up to date.
    const again = await Server.start(box, { workspace: server.workspace });
    try {
      await waitForOutput(again, 'Server waiting for client requests on');
      expect(logMessages(again.output, 'INFO')).toContain('Database is up to date.');
      expect(again.output).toMatch(
        /│ Product endpoint +│ Database status +│ Database location +│ Schema version in the database +│ Schema version in the package +│/,
      );
      expect(again.output).toMatch(/│ Default +│ Database is up to date\. +│/);
    } finally {
      await again.stop();
    }
  });

  for (const args of [['--db-status', 'all'], ['--db-status', 'Default']]) {
    useCase('server-database.md', 'Show the database status', async () => {
      // The option is ignored and a normal server starts.
      const server = await Server.start(box, { args });
      try {
        await waitForOutput(server, 'Server waiting for client requests on');
        await sleep(2000);
        expect(server.running).toBe(true);
      } finally {
        await server.stop();
      }
    }, { name: args.join(' '), knownIssue: 1 });
  }

  for (const args of [['--db-upgrade-schema', 'all'], ['--db-upgrade-schema', 'Default'], ['--db-force-upgrade']]) {
    useCase('server-database.md', 'Upgrade product databases', async () => {
      const server = await Server.start(box, { args });
      try {
        await waitForOutput(server, 'Server waiting for client requests on');
        await sleep(2000);
        expect(server.running).toBe(true);
      } finally {
        await server.stop();
      }
    }, { name: args.join(' '), knownIssue: 1 });
  }

  useCase('server-database.md', 'Upgrade product databases', async () => {
    const result = await cc(['server', '-w', box.newDir('ws'), '--db-upgrade-schema', 'all', '--db-force-upgrade'], { cwd: box.dir });
    expect(result).toExitWith(1);
    expect(result.output).toContain('argument --db-force-upgrade: not allowed with argument --db-upgrade-schema');
  }, { name: 'the options exclude each other', knownIssue: 'H4' });
});
