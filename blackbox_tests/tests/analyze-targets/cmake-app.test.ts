// blackbox_tests/analyze-targets/cmake-app

import { readdirSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { logMessages } from '../../src/normalize';
import { DOC_ANALYZERS, analyzeProject, copyProject, projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, plainReports, reportRows } from '../../src/reports';

const box = useSandbox();
const table = () => markdownTable(projectReadme('cmake-app'), 'Expected results');

describe('project cmake-app', () => {
  test('logged build: the README table', async () => {
    const app = await sharedAnalysis('cmake-app');
    expectRowsToMatchTable(await reportRows(app.reports, app.root), table());
    const parsed = await cc(['parse', app.reports], { cwd: app.root });
    expect(parsed).toExitWith(2);
    // The header report comes from two analyses and is printed twice.
    expect(plainReports(parsed.stdout).filter((r) => r.path.endsWith('/item.h'))).toHaveLength(2);
    expect(parsed.stdout.split('Found 1 defect(s) in item.h').length - 1).toBe(2);
  });

  test("CMake's own compile_commands.json gives the same reports", async () => {
    const app = await analyzeProject(box, 'cmake-app');
    const own = path.join(app.root, 'cmake-reports');
    expect(await cc(['analyze', 'build/compile_commands.json', '-o', own, ...DOC_ANALYZERS], { cwd: app.root })).toExitWith(0);
    expectRowsToMatchTable(await reportRows(own, app.root), table());
  });

  test('the skipfile removes the third-party report', async () => {
    const app = await analyzeProject(box, 'cmake-app', ['-i', 'skipfile']);
    const rows = await reportRows(app.reports, app.root);
    expectRowsToMatchTable(rows, table().filter((r) => !r.Location.startsWith('third_party/')));
  });

  test('logging the configure step too makes analyze fail on CMake checks', async () => {
    const root = await copyProject('cmake-app', box.newDir('configure'));
    expect(await cc(['log', '-b', 'cmake -S . -B fresh && cmake --build fresh', '-o', 'compile_commands.json'], { cwd: root })).toExitWith(0);
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(3);
    expect(logMessages(result.output, 'ERROR').some((l) => /Analyzing CMakeCXXCompilerABI\.cpp with clangsa +failed!/.test(l))).toBe(true);
    const failed = readdirSync(path.join(root, 'reports', 'failed'));
    expect(failed.some((f) => /^CMakeCXXCompilerABI\.cpp_clangsa_[0-9a-f]{32}\.plist_compile_error\.zip$/.test(f))).toBe(true);
  });
});
