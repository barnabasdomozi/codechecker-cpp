// testing/analyze-targets/ctu

import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { DOC_ANALYZERS, analyzeProject, projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, reportRows } from '../../src/reports';

const box = useSandbox();
const readme = () => projectReadme('ctu');

describe('project ctu', () => {
  test('without CTU', async () => {
    const p = await sharedAnalysis('ctu');
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(readme(), 'Without CTU'));
  });

  test('with CTU', async () => {
    const p = await sharedAnalysis('ctu', ['--ctu']);
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(readme(), 'With CTU'));
    const steps = await cc(['parse', p.reports, '--print-steps'], { cwd: p.root });
    expect(steps.stdout).toContain(
      [
        '  Steps:',
        "    1, pipeline_a.cpp:6:16: Assuming 'value' is > 0",
        "    2, pipeline_a.cpp:6:3: 'input' initialized to a null pointer value",
        "    3, pipeline_a.cpp:7:18: Passing null pointer value via 1st parameter 'value'",
        "    4, pipeline_a.cpp:7:10: Calling 'stage_b'",
        "    5, pipeline_b.cpp:3:1: Entered call from 'stage_a'",
        "    6, pipeline_b.cpp:3:42: Passing null pointer value via 1st parameter 'value'",
        "    7, pipeline_b.cpp:3:34: Calling 'stage_c'",
        "    8, pipeline_c.cpp:3:1: Entered call from 'stage_b'",
        "    9, pipeline_c.cpp:3:34: Dereference of null pointer (loaded from variable 'value')",
      ].join('\n'),
    );
  });

  for (const args of [['--ctu-ast-mode', 'load-from-pch'], ['--ctu-ast-mode', 'parse-on-demand']]) {
    test(`with CTU, ${args.join(' ')}`, async () => {
      const p = await analyzeProject(box, 'ctu', ['--ctu', ...args]);
      expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(readme(), 'With CTU'));
    });
  }

  test('collect and analyze phases separately', async () => {
    const p = await analyzeProject(box, 'ctu', ['--ctu-collect']);
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--ctu-analyze'], { cwd: p.root })).toExitWith(0);
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(readme(), 'With CTU'));
  });
});
