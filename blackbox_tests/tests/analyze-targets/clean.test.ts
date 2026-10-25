// blackbox_tests/analyze-targets/clean

import { describe, expect, test } from '@jest/globals';
import { cc, run } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { DOC_ANALYZERS, analyzeProject, projectCopy, sharedAnalysis } from '../../src/projects';
import { reportRows } from '../../src/reports';

const box = useSandbox();

describe('project clean', () => {
  test('no reports; parse exits 0', async () => {
    const p = await sharedAnalysis('clean');
    expect(await reportRows(p.reports, p.root)).toEqual([]);
    const parsed = await cc(['parse', p.reports], { cwd: p.root });
    expect(parsed).toExitWith(0);
    for (const file of ['main.cpp', 'tokenizer.cpp', 'word_count.cpp']) expect(parsed.stdout).toContain(`Found no defects in ${file}`);
    expect(parsed.stdout).toContain('│ Number of processed analyzer result files │ 9 │');
    expect(parsed.stdout).toContain('│ Number of analyzer reports                │ 0 │');
  });

  test('the program works', async () => {
    const root = await projectCopy(box, 'clean');
    expect(await run('make', [], { cwd: root })).toExitWith(0);
    const result = await run('./wordstat', [], { cwd: root, input: 'x y x' });
    expect(result.stdout).toBe('3 words, 2 distinct\nx 2\ny 1\n');
  });

  test('check -b make exits 0; CTU and sensitive find nothing; extreme finds style issues', async () => {
    const root = await projectCopy(box, 'clean');
    expect(await cc(['check', '-b', 'make', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    for (const args of [['--ctu'], ['-e', 'profile:sensitive']]) {
      const p = await analyzeProject(box, 'clean', args);
      expect(await reportRows(p.reports, p.root)).toEqual([]);
    }
    const extreme = await analyzeProject(box, 'clean', ['-e', 'profile:extreme']);
    expect(await reportRows(extreme.reports, extreme.root)).toHaveLength(23);
  });
});
