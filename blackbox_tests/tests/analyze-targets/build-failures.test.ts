// blackbox_tests/analyze-targets/build-failures

import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { logMessages } from '../../src/normalize';
import { DOC_ANALYZERS, copyProject, projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, reportRows } from '../../src/reports';

const box = useSandbox();
const table = () => markdownTable(projectReadme('build-failures'), '`make -k`');
const failedKinds = (dir: string) =>
  readdirSync(path.join(dir, 'failed')).map((f) => f.replace(/_[0-9a-f]{32}\.plist_compile_error\.zip$/, '')).sort();

describe('project build-failures', () => {
  test('make -k: the README table', async () => {
    const p = await sharedAnalysis('build-failures', [], 3);
    expect(p.analyzeExitCode).toBe(3);
    expectRowsToMatchTable(await reportRows(p.reports, p.root), table());
    expect(failedKinds(p.reports)).toEqual(
      ['broken_syntax.cpp', 'gcc_only_flag.cpp', 'missing_header.cpp'].flatMap((f) => [`${f}_clang-tidy`, `${f}_clangsa`]).sort(),
    );
    const entries = JSON.parse(readFileSync(p.compileDb, 'utf8')) as unknown[];
    expect(entries).toHaveLength(5);
  });

  test('make (stops at the first error)', async () => {
    const root = await copyProject('build-failures', box.newDir('make'));
    const logged = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root });
    expect(logged).toExitWith(2);
    expect((JSON.parse(readFileSync(path.join(root, 'compile_commands.json'), 'utf8')) as unknown[]).length).toBe(4);
    const analyzed = await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root });
    expect(analyzed).toExitWith(3);
    const info = logMessages(analyzed.output, 'INFO');
    const failedAt = info.indexOf('Failed to analyze');
    expect(info.slice(failedAt + 1, failedAt + 3)).toEqual(['  clangsa: 2', '  clang-tidy: 2']);
    expectRowsToMatchTable(await reportRows(path.join(root, 'reports'), root), table().filter((r) => !r.Location.startsWith('missing_header')));
  });

  test('check -b make stops after the failed build', async () => {
    const root = await copyProject('build-failures', box.newDir('check'));
    const result = await cc(['check', '-b', 'make', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    expect(logMessages(result.output, 'INFO')).toContain('Build failed.');
  });
});
