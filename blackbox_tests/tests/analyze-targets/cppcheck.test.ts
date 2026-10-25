// Every test project analyzed with `--analyzers cppcheck` gives exactly the
// Cppcheck rows of its expected-results table.

import { describe, expect } from '@jest/globals';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { demoProjectDoc, projectReadme, sharedAnalysis, type ProjectName } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, reportRows, type TableRow } from '../../src/reports';

useSandbox();


/** The expected-results table of each project (null: the project has no reports). */
const CASES: Array<[ProjectName, () => TableRow[] | null]> = [
  ['demo', () => markdownTable(demoProjectDoc(), '1. `demo`')],
  ['demo2', () => markdownTable(demoProjectDoc(), '2. `demo2`')],
  ['cmake-app', () => markdownTable(projectReadme('cmake-app'), 'Expected results')],
  ['make-recursive', () => markdownTable(projectReadme('make-recursive'), 'Expected results')],
  ['ctu', () => markdownTable(projectReadme('ctu'), '### Without CTU')],
  ['review-status', () => markdownTable(projectReadme('review-status'), 'Expected results (`make`)')],
  ['build-failures', () => markdownTable(projectReadme('build-failures'), '### `make -k`')],
  ['clean', () => null],
  ['odd-paths', () => markdownTable(projectReadme('odd-paths'), 'Expected results')],
  ['analyzer-features', () => markdownTable(projectReadme('analyzer-features'), '### Default checkers')],
];

describe('test projects with Cppcheck', () => {
  for (const [name, table] of CASES) {
    // "Run only some analyzers" of usage/analyze.md, for every project.
    useCase('analyze.md', 'Run only some analyzers', async () => {
      const p = await sharedAnalysis(name, ['--analyzers', 'cppcheck']);
      const rows = await reportRows(p.reports, p.root);
      expect(rows.every((r) => r.analyzer === 'cppcheck')).toBe(true);
      const expected = (table() ?? []).filter((r) => r.Analyzer === 'cppcheck');
      if (expected.length === 0) expect(rows).toEqual([]);
      else expectRowsToMatchTable(rows, expected);
    }, { name: `cppcheck on project ${name}`, requires: ['command:cppcheck'] });
  }
});
