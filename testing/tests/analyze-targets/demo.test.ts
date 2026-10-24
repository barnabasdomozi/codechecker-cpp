// testing/analyze-targets/demo, against docs/reference/demo-project.md.

import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { config } from '../../src/config';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, plainLocations, plainReports, reportRows } from '../../src/reports';

useSandbox();
const reference = () => path.join(config().docsDir, 'reference', 'demo-project.md');

describe('project demo', () => {
  test('v1: the table of reference/demo-project.md §1', async () => {
    const demo = await sharedAnalysis('demo');
    expectRowsToMatchTable(await reportRows(demo.reports, demo.root), markdownTable(reference(), '1. `demo`'));
    const parsed = await cc(['parse', demo.reports], { cwd: demo.root });
    expect(parsed).toExitWith(2);
    expect(plainLocations(plainReports(parsed.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });

  test('v2: the table of reference/demo-project.md §2', async () => {
    const demo2 = await sharedAnalysis('demo2');
    expectRowsToMatchTable(await reportRows(demo2.reports, demo2.root), markdownTable(reference(), '2. `demo2`'));
  });

  test('CTU finds main.c:6', async () => {
    const ctu = await sharedAnalysis('demo', ['--ctu', '--analyzers', 'clangsa']);
    const rows = await reportRows(ctu.reports, ctu.root);
    expect(rows.map((r) => r.location)).toContain('main.c:6:12');
  });
});
