// docs/usage/local-diff.md
//
// `reports` is the demo, `reports2` is demo2 analyzed with
// `-e modernize-use-nullptr`, as in the docs.

import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines, logMessages, normalize } from '../src/normalize';
import { sharedAnalysis, type Analysis } from '../src/projects';
import { plainLocations, plainReports } from '../src/reports';

const box = useSandbox();
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2', ['-e', 'modernize-use-nullptr'])]);
});

const diff = (args: string[], env?: Record<string, string | undefined>) => cc(['cmd', 'diff', ...args], { cwd: box.dir, env });
const NEW = ['main.c:21:16', 'main.c:21:18', 'style.cpp:4:12', 'style.cpp:4:35', 'style.cpp:4:35'];

describe('usage/local-diff.md', () => {
  useCase('local-diff.md', 'New reports', async () => {
    const result = await diff(['-b', demo.reports, '-n', demo2.reports, '--new']);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(NEW);
    const out = normalize(result.output, { paths: { '~/demo2': demo2.root } });
    expect(out).toContain("[HIGH] ~/demo2/main.c:21:18: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]\n  int *q = 0; *q = 1;\n                 ^\n");
    expect(out).toContain('[LOW] ~/demo2/style.cpp:4:12: use nullptr [modernize-use-nullptr]');
    expect(out).toContain('[HIGH] ~/demo2/style.cpp:4:35: Division by zero [core.DivideZero]');
    expect(out).toContain('[HIGH] ~/demo2/main.c:21:16: Null pointer dereference: q [cppcheck-nullPointer]');
    expect(out).toContain('[HIGH] ~/demo2/style.cpp:4:35: Division by zero. [cppcheck-zerodiv]');
    expect(result.stdout).toContain('│ Number of processed analyzer result files │ 0 │');
    expect(result.stdout).toContain('│ Number of analyzer reports                │ 5 │');
    const info = logMessages(result.output, 'INFO');
    expect(info).toContain(`Matching local report directories (--baseline): ${demo.reports}`);
    expect(info).toContain(`Matching local report directories (--newname): ${demo2.reports}`);
    expect(info).toContain(`Compared the following local files / directories: ${demo.reports} and ${demo2.reports}`);
  });

  useCase('local-diff.md', 'Resolved and unresolved reports', async () => {
    const resolved = await diff(['-b', demo.reports, '-n', demo2.reports, '--resolved']);
    expect(resolved).toExitWith(2);
    expect(plainReports(resolved.stdout).map((r) => r.checker)).toEqual(['clang-diagnostic-unused-variable']);

    const unresolved = await diff(['-b', demo.reports, '-n', demo2.reports, '--unresolved']);
    expect(unresolved).toExitWith(2);
    expect(plainLocations(plainReports(unresolved.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });

  useCase('local-diff.md', 'Resolved and unresolved reports', async () => {
    const table = await diff(['-b', demo.reports, '-n', demo2.reports, '--unresolved', '-o', 'table']);
    expect(table).toExitWith(2);
    expect(table.stdout).toMatch(/│ File +│ Checker +│ Severity +│ Msg +│ Source +│/);
    // The columns are shifted: "File" holds the severity, "Checker" the file.
    const row = lines(normalize(table.stdout, { paths: { '~/demo2': demo2.root } })).find((l) => l.includes('main.c:11:12') && l.includes('core.DivideZero'))!;
    expect(row).toMatch(/^│ HIGH +│ ~\/demo2\/main\.c:11:12 +│ Division by zero +│ core\.DivideZero +│ +return a \/ z; +│$/);
  }, { name: 'table headers do not match the data', knownIssue: 4 });

  useCase('local-diff.md', 'Compare against a baseline file', async () => {
    const baseline = path.join(box.newDir('baseline'), 'reports.baseline');
    expect(await cc(['parse', demo.reports, '-e', 'baseline', '-o', baseline], { cwd: box.dir })).toExitWith(2);
    const result = await diff(['-b', baseline, '-n', demo2.reports, '--new']);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(NEW);
    expect(logMessages(result.output, 'INFO')).toContain(`Matching local baseline files (--baseline): ${baseline}`);

    // Folder as base, baseline as new side: --resolved prints the folder's
    // reports that are not in the baseline.
    const resolved = await diff(['-b', demo2.reports, '-n', baseline, '--resolved']);
    expect(resolved).toExitWith(2);
    expect(plainLocations(plainReports(resolved.stdout))).toEqual(NEW);
  });

  useCase('local-diff.md', 'Compare against a baseline file', async () => {
    const baseline = path.join(box.newDir('baseline2'), 'reports.baseline');
    expect(await cc(['parse', demo.reports, '-e', 'baseline', '-o', baseline], { cwd: box.dir })).toExitWith(2);
    const result = await diff(['-b', demo2.reports, '-n', baseline, '--new']);
    expect(plainReports(result.stdout)).toEqual([]);
    expect(result).toExitWith(2);
  }, { name: 'folder vs baseline --new finds nothing but exits 2', knownIssue: 5 });

  useCase('local-diff.md', 'No differences', async () => {
    const result = await diff(['-b', demo.reports, '-n', demo.reports, '--new']);
    expect(result).toExitWith(0);
    expect(plainReports(result.stdout)).toEqual([]);
  });

  useCase('local-diff.md', 'Filters', async () => {
    const result = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--severity', 'high', '--checker-name', 'core.*', '--print-steps']);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:21:18', 'style.cpp:4:35']);
    expect(result.stdout).toContain('  Report hash: 19a15bf46b7d3af4b189b3b4f2c28a5b\n  Steps:');

    const msg = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--checker-msg', '*NULLPTR*']);
    expect(plainLocations(plainReports(msg.stdout))).toEqual(['style.cpp:4:12']);

    const byName = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--file', 'main.c']);
    expect(plainReports(byName.stdout)).toEqual([]);
    const byPath = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--file', '*/main.c']);
    expect(plainLocations(plainReports(byPath.stdout))).toEqual(['main.c:21:16', 'main.c:21:18']);
  });

  useCase('local-diff.md', 'Filters', async () => {
    // False positives count as resolved; asking for them shows them.
    const fp = await diff(['-b', demo.reports, '-n', demo2.reports, '--unresolved', '--review-status', 'false_positive']);
    expect(plainLocations(plainReports(fp.stdout))).toEqual([
      'main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10',
      'style.cpp:11:12', 'style.cpp:11:12', 'style.cpp:17:12', 'style.cpp:17:12',
    ]);
  }, { name: '--review-status always includes unreviewed' });

  useCase('local-diff.md', 'Filters', async () => {
    const base = await diff(['-b', demo.reports, '-n', demo2.reports, '--new']);
    for (const extra of [
      ['--report-hash', '0000'],
      ['--analyzer-name', 'nothing'],
      ['--bug-path-length', '100:'],
      ['--detection-status', 'resolved'],
      ['--report-status', 'closed'],
    ]) {
      const result = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', ...extra]);
      expect(result).toExitWith(2);
      expect(plainLocations(plainReports(result.stdout))).toEqual(plainLocations(plainReports(base.stdout)));
    }
  }, { name: 'other filters are ignored for local folders', knownIssue: 15 });

  useCase('local-diff.md', 'Filters', async () => {
    const unknown = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--review-status', 'suppress']);
    expect(unknown).toExitWith(1);
    expect(unknown.output).toContain('KeyError');
  }, { name: 'an unknown --review-status value crashes', knownIssue: 16 });

  useCase('local-diff.md', 'Filters', async () => {
    const component = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '--component', 'x']);
    expect(component).toExitWith(1);
    expect(component.output).toContain('AttributeError');
  }, { name: '--component crashes', knownIssue: 17 });

  useCase('local-diff.md', 'Output formats', async () => {
    const dir = box.newDir('formats');
    const html = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'html', '-e', path.join(dir, 'diff_html')]);
    expect(html).toExitWith(2);
    expect(existsSync(path.join(dir, 'diff_html', 'index.html'))).toBe(true);
    expect(html.output).toContain(`To view the results in a browser run:\n> firefox ${path.join(dir, 'diff_html')}/index.html`);

    const json = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'json', '-e', path.join(dir, 'diff_json')]);
    expect(json).toExitWith(2);
    expect(logMessages(json.output, 'INFO')).toContain(`JSON report file was created: ${path.join(dir, 'diff_json')}/reports.json`);
    const fromFile = JSON.parse(readFileSync(path.join(dir, 'diff_json', 'reports.json'), 'utf8')) as { reports: unknown[] };
    expect(fromFile.reports).toHaveLength(5);
    const toStdout = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'json']);
    expect((JSON.parse(toStdout.stdout) as { reports: unknown[] }).reports).toHaveLength(5);

    const codeclimate = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'codeclimate', '-e', path.join(dir, 'diff_cc')]);
    expect(codeclimate).toExitWith(2);
    expect(JSON.parse(readFileSync(path.join(dir, 'diff_cc', 'codeclimate_issues.json'), 'utf8'))).toHaveLength(5);
  });

  useCase('local-diff.md', 'Output formats', async () => {
    const dir = box.newDir('formats2');
    const changed = box.write(path.join(dir, 'changed.json'), JSON.stringify({ 'main.c': { lines_inserted: 1 }, 'style.cpp': { lines_inserted: 1 } }));
    const gerrit = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'gerrit', '-e', path.join(dir, 'diff_gerrit')], {
      CC_CHANGED_FILES: changed,
      CC_REPO_DIR: demo2.root,
    });
    expect(gerrit).toExitWith(2);
    const review = JSON.parse(readFileSync(path.join(dir, 'diff_gerrit', 'gerrit_review.json'), 'utf8')) as { message: string };
    expect(review.message).toBe('CodeChecker found 5 issue(s) in the code.');

    const out = path.join(dir, 'out');
    box.write(path.join(out, 'stale.txt'), 'old');
    const several = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'plaintext', 'html', '-e', out, '-c']);
    expect(several).toExitWith(2);
    expect(plainReports(several.stdout)).toHaveLength(5);
    expect(existsSync(path.join(out, 'index.html'))).toBe(true);
    expect(existsSync(path.join(out, 'stale.txt'))).toBe(false);

    const noExport = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'html']);
    expect(noExport).toExitWith(1);
    expect(logMessages(noExport.output, 'ERROR')).toContain('Argument --export not allowed without argument --output when exporting to HTML.');
  }, { name: 'gerrit, several formats, -c' });

  useCase('local-diff.md', 'Output formats', async () => {
    const result = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-c']);
    expect(result).toExitWith(1);
    expect(result.output).toContain('TypeError');
  }, { name: '-c without -e crashes', knownIssue: 17 });

  useCase('local-diff.md', 'Output formats', async () => {
    for (const format of ['rows', 'table', 'csv']) {
      const result = await diff(['-b', demo.reports, '-n', demo2.reports, '--new', '-o', format]);
      expect(result).toExitWith(2);
      expect(result.stdout).toContain('main.c:21:18');
    }
  }, { name: 'rows, table, csv' });

  useCase('local-diff.md', 'Exit codes', async () => {
    expect(await diff(['-b', demo.reports, '-n', demo.reports, '--new'])).toExitWith(0);
    expect(await diff(['-b', demo.reports, '-n', demo2.reports, '--new'])).toExitWith(2);
    expect(await diff(['-b', path.join(box.dir, 'missing'), '-n', demo2.reports, '--new'])).toExitWith(1);
  });
});
