// blackbox_tests/analyze-targets/review-status

import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { logMessages } from '../../src/normalize';
import { DOC_ANALYZERS, analyzeProject, projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, plainLocations, plainReports, reportRows } from '../../src/reports';

const box = useSandbox();
const readme = () => projectReadme('review-status');

describe('project review-status', () => {
  test('the README table', async () => {
    const p = await sharedAnalysis('review-status');
    const rows = await reportRows(p.reports, p.root);
    expectRowsToMatchTable(rows, markdownTable(readme(), 'Expected results (`make`)'));
    expect(rows.every((r) => r.severity === 'HIGH')).toBe(true);
  });

  test('plain parse shows unreviewed and confirmed reports, and the misspelling warning', async () => {
    const p = await sharedAnalysis('review-status');
    const result = await cc(['parse', p.reports], { cwd: p.root });
    expect(result).toExitWith(2);
    const shown = markdownTable(readme(), 'Expected results (`make`)')
      .filter((r) => ['unreviewed', 'confirmed'].includes(r['Review status']))
      .map((r) => r.Location.replace(/^.*\//, ''))
      .sort();
    expect(plainLocations(plainReports(result.stdout))).toEqual(shown);
    expect(logMessages(result.output, 'WARNING')).toContain(
      `${p.root}/comments.cpp contains misspelled review status comment @79: // codechecker_suppressss [all] contains a marker name, so parse warns`,
    );
    expect(result.output).not.toContain('@85');
  });

  test('with the rules file', async () => {
    const p = await analyzeProject(box, 'review-status', ['--review-status-config', 'review_status.yaml']);
    const rows = (await reportRows(p.reports, p.root)).filter((r) => r.file === 'rules_target.cpp');
    const table = markdownTable(readme(), 'With the rules file');
    expect(rows.map((r) => `${r.location} ${r.reviewStatus}`).sort()).toEqual(table.map((t) => `${t.Location} ${t['Review status']}`).sort());
  });

  test('with the suppress file', async () => {
    const p = await sharedAnalysis('review-status');
    const result = await cc(['parse', p.reports, '--suppress', path.join(p.root, 'suppress.txt')], { cwd: p.root });
    expect(result).toExitWith(2);
    // The entries have Clang SA hashes; Cppcheck's reports on these lines stay.
    const shown = plainReports(result.stdout)
      .filter((r) => r.checker === 'core.DivideZero')
      .map((r) => `${path.basename(r.path)}:${r.line}:${r.column}`);
    expect(shown).not.toContain('comments.cpp:6:12');
    expect(shown).not.toContain('comments.cpp:80:12');
    expect(shown).toContain('comments.cpp:86:12');
    expect(shown).toContain('comments.cpp:92:12');
    const cppcheck = plainReports(result.stdout).filter((r) => r.checker === 'cppcheck-zerodiv').map((r) => r.line);
    expect(cppcheck).toEqual(expect.arrayContaining([6, 80]));
    expect(logMessages(result.output, 'WARNING')).toContain('Malformed suppress line: this line is malformed');
  });

  test('ambiguous comments make parse fail', async () => {
    const p = await analyzeProject(box, 'review-status');
    expect(await cc(['log', '-b', 'make ambiguous', '-o', 'amb.json'], { cwd: p.root })).toExitWith(0);
    expect(await cc(['analyze', 'amb.json', '-o', 'amb', ...DOC_ANALYZERS], { cwd: p.root })).toExitWith(0);
    const result = await cc(['parse', 'amb'], { cwd: p.root });
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain(
      `Multiple source code comments can be found for 'core.DivideZero' checker in '${p.root}/extra/ambiguous.cpp' at line 7.`,
    );
    expect(await cc(['parse', 'amb', '-e', 'json'], { cwd: p.root })).toExitWith(1);
  });
});
