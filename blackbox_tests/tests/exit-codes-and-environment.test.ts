// docs/usage/exit-codes.md and environment-variables.md: the claims not
// covered by the use-case tests of the other pages.

import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc, tool } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { freePort } from '../src/server';
import { isAvailable } from '../src/requirements';
import { lines } from '../src/normalize';

const box = useSandbox();

describe('usage/exit-codes.md', () => {
  test('argument errors of CodeChecker commands exit 1, not 2', async () => {
    for (const args of [
      ['analyze', 'x.json', '-o', 'r', '--checker-config', 'clang-tidy:a.b=1'],
      ['parse', '--no-such-option'],
      ['analyze'],
      ['cmd', 'runs', '--no-such-option'],
    ]) {
      expect(await cc(args, { cwd: box.dir })).toExitWith(1);
    }
  });

  test('argument errors of the standalone tools exit 2', async () => {
    for (const name of ['report-converter', 'tu_collector', 'merge-clang-extdef-mappings', 'post-process-stats', 'memory_safety_reporter', 'bazel-compile-commands']) {
      if (!isAvailable(`tool:${name}`)) continue;
      expect(await tool(name, ['--no-such-option'], { cwd: box.dir })).toExitWith(2);
    }
  });

  test('version, analyzers and checkers exit 0', async () => {
    for (const args of [['version'], ['analyzer-version'], ['web-version'], ['analyzers'], ['checkers']]) {
      expect(await cc(args, { cwd: box.dir })).toExitWith(0);
    }
  });

  test('client commands exit 1 on connection errors and 2 on a malformed product URL', async () => {
    const port = await freePort();
    expect(await cc(['cmd', 'runs', '--url', `localhost:${port}/Default`], { cwd: box.dir })).toExitWith(1);
    expect(await cc(['cmd', 'products', 'list', '--url', `localhost:${port}`], { cwd: box.dir })).toExitWith(1);
    expect(await cc(['cmd', 'runs', '--url', 'localhost:notaport/Default'], { cwd: box.dir })).toExitWith(2);
  });
});

describe('usage/environment-variables.md', () => {
  test('a client call creates ~/.codechecker.session.json', async () => {
    const port = await freePort();
    await cc(['cmd', 'runs', '--url', `localhost:${port}/Default`], { cwd: box.dir });
    const file = path.join(box.home, '.codechecker.session.json');
    expect(existsSync(file)).toBe(true);
    expect(JSON.parse(readFileSync(file, 'utf8'))).toEqual({ tokens: {} });
  });

  test('CC_SESSION_FILE moves the session file', async () => {
    const port = await freePort();
    const file = path.join(box.newDir('session'), 'my-session.json');
    await cc(['cmd', 'runs', '--url', `localhost:${port}/Default`], { cwd: box.dir, env: { CC_SESSION_FILE: file } });
    expect(existsSync(file)).toBe(true);
  });

  test('COLUMNS sets the table width', async () => {
    // Long descriptions are wrapped to the width.
    const args = ['checkers', '--details', '--analyzers', 'clangsa', '-o', 'table'];
    const narrow = await cc(args, { cwd: box.dir, env: { COLUMNS: '100' } });
    const wide = await cc(args, { cwd: box.dir, env: { COLUMNS: '250' } });
    const width = (text: string) => Math.max(...lines(text).map((l) => [...l].length));
    expect(width(narrow.stdout)).toBeLessThan(width(wide.stdout));
  });

  test('the default server workspace is ~/.codechecker', async () => {
    const result = await cc(['server', '-l'], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(result.output).toContain(`Workspace directory: ${box.home}/.codechecker`);
  });
});
