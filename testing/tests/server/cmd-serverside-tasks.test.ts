// docs/usage/cmd-serverside-tasks.md

import { beforeAll, describe, expect } from '@jest/globals';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages } from '../../src/normalize';
import { sharedAnalysis, type Analysis } from '../../src/projects';
import { privateReports, useServer } from '../../src/server';

const box = useSandbox();
const server = useServer(box);
let demo: Analysis;

beforeAll(async () => {
  demo = await sharedAnalysis('demo');
});

const tasks = (args: string[]) => server().cc(['cmd', 'serverside-tasks', ...args, '--url', server().serverUrl]);

async function detachedStore(run: string): Promise<string> {
  const result = await server().cc(['store', privateReports(demo.reports), '-n', run, '--detach', '--url', server().url()]);
  expect(result).toExitWith(0);
  return lines(result.stdout).filter((l) => l.trim() !== '').pop()!;
}

describe('usage/cmd-serverside-tasks.md', () => {
  useCase('cmd-serverside-tasks.md', 'Query one task by token', async () => {
    const token = await detachedStore('detached');
    expect(await tasks(['-t', token, '--await'])).toExitWith(0);
    const result = await tasks(['-t', token]);
    expect(result).toExitWith(0);
    const out = result.stdout;
    expect(out).toContain(`Task '${token}':`);
    expect(out).toMatch(/- Type: +report_server::massStoreRunAsynchronous\(\)/);
    expect(out).toMatch(/- Summary: +Store of results to 'Default' - 'detached'/);
    expect(out).toMatch(/- Status: +COMPLETED/);
    expect(out).toMatch(/- Product: +Default/);
    expect(out).toMatch(/- User: +Anonymous/);
    for (const field of ['Enqueued at', 'Started at', 'Last seen', 'Completed at']) {
      expect(out).toMatch(new RegExp(`- ${field}: +\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d`));
    }
  });

  useCase('cmd-serverside-tasks.md', 'Wait for a task', async () => {
    const token = await detachedStore('awaited');
    const result = await tasks(['-t', token, '--await']);
    expect(result).toExitWith(0);
    const json = await tasks(['-t', token, '--output', 'json']);
    expect(json).toExitWith(0);
    expect(json.stdout).toContain('COMPLETED');
  });

  useCase('cmd-serverside-tasks.md', 'List tasks with filters', async () => {
    const token = await detachedStore('listed');
    expect(await tasks(['-t', token, '--await'])).toExitWith(0);

    const table = await tasks(['--status', 'completed', '--product', 'Default', '--output', 'table']);
    expect(table).toExitWith(0);
    // Long tokens are wrapped in the table.
    expect(table.stdout).toContain(token.slice(0, 30));
    expect(table.stdout).toMatch(/│ Token +│ Machine +│ Type +│ Summary +│ Status +│ Product +│ User +│/);

    const byType = await tasks(['--type', 'report_server::massStoreRunAsynchronous()', '--output', 'json']);
    expect(byType).toExitWith(0);
    const list = JSON.parse(byType.stdout) as Array<{ token: string; taskKind: string; machineId: string }>;
    expect(list.map((t) => t.token)).toContain(token);
    expect(list.every((t) => t.taskKind === 'report_server::massStoreRunAsynchronous()')).toBe(true);
    expect(list[0].machineId).toMatch(new RegExp(`:${server().port}$`));

    const recent = await tasks(['--enqueued-after', '2020:01:01', '--no-cancelled', '--output', 'json']);
    expect((JSON.parse(recent.stdout) as Array<{ token: string }>).map((t) => t.token)).toContain(token);

    // --type must match exactly.
    const inexact = await tasks(['--type', 'TaskInfo', '--output', 'json']);
    expect(JSON.parse(inexact.stdout)).toEqual([]);
  });

  useCase('cmd-serverside-tasks.md', 'List tasks with filters', async () => {
    const result = await tasks([]);
    expect(result).toExitWith(2);
    expect(logMessages(result.output, 'ERROR')).toContain(
      "ERROR! To use 'CodeChecker cmd serverside-tasks', a '--token' list or some other filter criteria **MUST** be specified!",
    );
  }, { name: 'a token or a filter is required' });

  useCase('cmd-serverside-tasks.md', 'Cancel tasks', async () => {
    const token = await detachedStore('to_kill');
    expect(await tasks(['-t', token, '--await'])).toExitWith(0);
    const result = await tasks(['--status', 'completed', '--kill']);
    expect(result).toExitWith(0);
    // The matched tasks were already completed, so they are only listed.
    expect(result.output).toContain(token.slice(0, 30));
    const after = await tasks(['-t', token, '--output', 'json']);
    expect(after.stdout).toContain('"status": "COMPLETED"');
  });
});
