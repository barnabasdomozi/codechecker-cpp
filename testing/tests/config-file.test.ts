// docs/usage/config-file.md (analyze, parse and check; the store and server
// sections are in tests/server/config-file-server.test.ts)

import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { DOC_ANALYZERS, loggedProject } from '../src/projects';
import { checkerCounts, parseJson, plainReports } from '../src/reports';
import { readdirSync } from 'node:fs';

const box = useSandbox();
let root = '';
let counter = 0;

const YAML = `analyze:
  - --enable=core.DivideZero
  - --disable-all
  - --analyzers=clangsa
  - --report-hash=context-free-v2
parse:
  - --print-steps
`;

const STEPS = [
  '  Report hash: d11a6fc279b8946a4bdc0365ff032ae0',
  '  Steps:',
  "    1, main.c:22:18: Calling 'local_div'",
  "    2, main.c:9:1: Entered call from 'main'",
  "    3, main.c:10:3: 'z' initialized to 0",
  '    4, main.c:11:12: Division by zero',
].join('\n');

beforeAll(async () => {
  root = await loggedProject(box, 'demo');
});

function out(): string {
  counter += 1;
  return path.join(root, `reports-${counter}`);
}

function analyzers(dir: string): string[] {
  return [...new Set(readdirSync(dir).filter((f) => f.endsWith('.plist')).map((f) => f.split('_')[1]))].sort();
}

describe('usage/config-file.md', () => {
  useCase('config-file.md', 'YAML file', async () => {
    const config = box.write(path.join(root, 'codechecker.yaml'), YAML);
    const reports = out();
    expect(await cc(['analyze', 'compile_commands.json', '-o', reports, ...DOC_ANALYZERS, '--config', config], { cwd: root })).toExitWith(0);
    const parsed = await cc(['parse', reports, '--config', config], { cwd: root });
    expect(parsed).toExitWith(2);
    expect(plainReports(parsed.stdout).map((r) => `${path.basename(r.path)}:${r.line}:${r.column} [${r.checker}]`)).toEqual([
      'main.c:11:12 [core.DivideZero]',
    ]);
    expect(parsed.stdout).toContain(STEPS);
  });

  useCase('config-file.md', 'YAML file', async () => {
    const config = box.write(path.join(root, 'two-items.yaml'), 'analyze:\n  - --analyzers\n  - clangsa\n  - --disable-all\n  - --enable\n  - core.DivideZero\n');
    const reports = out();
    expect(await cc(['analyze', 'compile_commands.json', '-o', reports, ...DOC_ANALYZERS, '--config', config], { cwd: root })).toExitWith(0);
    const { json } = await parseJson([reports], root, []);
    expect(checkerCounts(json.reports)).toEqual({ 'core.DivideZero': 1 });
  }, { name: 'an option and its value as two list items' });

  useCase('config-file.md', 'YAML file', async () => {
    // The file's options are added after the command line, so they win.
    const config = box.write(path.join(root, 'only-sa.yaml'), 'analyze:\n  - --analyzers=clangsa\n');
    const reports = out();
    expect(
      await cc(['analyze', 'compile_commands.json', '-o', reports, '--analyzers', 'clang-tidy', '--config', config], { cwd: root }),
    ).toExitWith(0);
    expect(analyzers(reports)).toEqual(['clangsa']);
  }, { name: 'the file wins over the command line' });

  useCase('config-file.md', 'YAML file', async () => {
    const config = box.write(path.join(root, 'env.yaml'), 'analyze:\n  - --analyzers=${CC_TEST_ANALYZER}\n');
    const reports = out();
    const result = await cc(['analyze', 'compile_commands.json', '-o', reports, ...DOC_ANALYZERS, '--config', config], {
      cwd: root,
      env: { CC_TEST_ANALYZER: 'clang-tidy' },
    });
    expect(result).toExitWith(0);
    expect(analyzers(reports)).toEqual(['clang-tidy']);
  }, { name: 'environment variables are expanded' });

  useCase('config-file.md', 'JSON file', async () => {
    const config = box.write(
      path.join(root, 'codechecker.json'),
      JSON.stringify({ analyze: ['--analyzers=clangsa', '--disable-all', '--enable=core.DivideZero'] }),
    );
    const reports = out();
    expect(await cc(['analyze', 'compile_commands.json', '-o', reports, ...DOC_ANALYZERS, '--config', config], { cwd: root })).toExitWith(0);
    const { json } = await parseJson([reports], root, []);
    expect(checkerCounts(json.reports)).toEqual({ 'core.DivideZero': 1 });
    expect(analyzers(reports)).toEqual(['clangsa']);
  });

  useCase('config-file.md', 'check uses the analyze and parse sections', async () => {
    const config = box.write(path.join(root, 'check.yaml'), YAML);
    const result = await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS, '--config', config], { cwd: root });
    expect(result).toExitWith(2);
    expect(plainReports(result.stdout).map((r) => r.checker)).toEqual(['core.DivideZero']);
    expect(result.stdout).toContain(STEPS);
  });
});
