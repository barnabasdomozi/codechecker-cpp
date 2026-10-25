// blackbox_tests/analyze-targets/analyzer-features

import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, fixitNames, markdownTable, reportRows } from '../../src/reports';
import { isAvailable } from '../../src/requirements';

useSandbox();
const readme = () => projectReadme('analyzer-features');

const fixits = async (reports: string, cwd: string) => {
  const result = await cc(['fixit', reports], { cwd });
  expect(result).toExitWith(0);
  return fixitNames(result.stdout).sort();
};

describe('project analyzer-features', () => {
  test('default checkers', async () => {
    const p = await sharedAnalysis('analyzer-features');
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(readme(), 'Default checkers'));
    const steps = await cc(['parse', p.reports, '--print-steps', '--file', '*/paths.cpp'], { cwd: p.root });
    expect(steps.stdout).toContain("    8, paths.cpp:21:10: Dereference of null pointer (loaded from variable 'target')");
  });

  test('-e profile:sensitive adds four reports', async () => {
    const base = await reportRows((await sharedAnalysis('analyzer-features')).reports, (await sharedAnalysis('analyzer-features')).root);
    const p = await sharedAnalysis('analyzer-features', ['-e', 'profile:sensitive']);
    const rows = await reportRows(p.reports, p.root);
    expect(rows).toHaveLength(27);
    const added = rows.filter((r) => !base.some((b) => b.hash === r.hash && b.location === r.location));
    expectRowsToMatchTable(added, markdownTable(readme(), '`-e profile:sensitive`'));
  });

  test('-e profile:extreme runs GCC too', async () => {
    const p = await sharedAnalysis('analyzer-features', ['-e', 'profile:extreme']);
    const rows = await reportRows(p.reports, p.root);
    expect(rows).toHaveLength(99);
    expectRowsToMatchTable(
      rows.filter((r) => r.analyzer === 'gcc'),
      markdownTable(readme(), '`-e profile:extreme`'),
    );
  });

  test('single checkers', async () => {
    const nullptr = await sharedAnalysis('analyzer-features', ['-e', 'modernize-use-nullptr']);
    const rows = await reportRows(nullptr.reports, nullptr.root);
    expect(rows).toHaveLength(24);
    expect(rows.find((r) => r.checker === 'modernize-use-nullptr')).toMatchObject({ location: 'style.cpp:27:30', hash: '6c402b0d0f171d950520975c981b4c50' });
    const gcc = await sharedAnalysis('analyzer-features', ['-e', 'gcc-analyzer-double-free']);
    const gccRows = await reportRows(gcc.reports, gcc.root);
    expect(gccRows).toHaveLength(24);
    expect(gccRows.filter((r) => r.analyzer === 'gcc').map((r) => `${r.location} ${r.hash}`)).toEqual(['memory.cpp:22:3 7a1d59e41f74391b327db12829fe0b19']);
  });

  test('fix-its', async () => {
    if (!isAvailable('command:clang-apply-replacements')) return;
    const p = await sharedAnalysis('analyzer-features');
    expect(await fixits(p.reports, p.root)).toEqual(['performance-move-const-arg']);
    const s = await sharedAnalysis('analyzer-features', ['-e', 'profile:sensitive']);
    expect(await fixits(s.reports, s.root)).toEqual(
      ['performance-for-range-copy', 'performance-move-const-arg', 'performance-unnecessary-copy-initialization', 'performance-unnecessary-value-param'],
    );
    const n = await sharedAnalysis('analyzer-features', ['-e', 'modernize-use-nullptr']);
    expect(await fixits(n.reports, n.root)).toEqual(['modernize-use-nullptr', 'performance-move-const-arg']);
  });
});
