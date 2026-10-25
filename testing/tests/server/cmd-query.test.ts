// docs/usage/cmd-runs-history.md, cmd-results.md, cmd-sum.md
//
// Server content, as in the docs: run `demo` stored twice (`v1` = demo,
// `v2` = demo2 with --trim-path-prefix), then run `multi` (both folders).

import { setTimeout as sleep } from 'node:timers/promises';
import { beforeAll, describe, expect } from '@jest/globals';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages, tableRows, withoutLogLines } from '../../src/normalize';
import { sharedAnalysis, type Analysis } from '../../src/projects';
import { results, runs, store, useServer } from '../../src/server';

const box = useSandbox();
const server = useServer(box);
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
  await store(server(), [demo.reports], 'demo', ['--tag', 'v1', '--description', 'first run']);
  await store(server(), [demo2.reports], 'demo', ['--tag', 'v2', '--trim-path-prefix', `${demo2.root}/`]);
  // Storage dates must differ for the --all-after/before-run filters.
  await sleep(1100);
  await store(server(), [demo.reports, demo2.reports], 'multi');
});

const url = () => server().url();
const cmd = (args: string[]) => server().cc(['cmd', ...args]);

describe('usage/cmd-runs-history.md', () => {
  useCase('cmd-runs-history.md', 'List runs', async () => {
    const result = await cmd(['runs', '--url', url()]);
    expect(result).toExitWith(0);
    const rows = tableRows(result.stdout);
    expect(rows[0]).toEqual([
      'Name',
      'Number of unresolved reports',
      'Analyzer statistics',
      'Storage date',
      'Version tag',
      'Duration',
      'Description',
      'CodeChecker version',
    ]);
    const demoRow = rows.find((r) => r[0] === 'demo')!;
    expect(demoRow.slice(1, 3)).toEqual(['9', 'clangsa (3/3), clang-tidy (3/3), cppcheck (3/3)']);
    expect(demoRow[4]).toBe('v2');
    expect(demoRow[5]).toBe('0:00:00');

    // (Client log lines can go to stdout too.)
    const table = await cmd(['runs', '-o', 'table', '--url', url()]);
    expect(withoutLogLines(table.stdout)).toBe(withoutLogLines(result.stdout));
    const plaintext = await cmd(['runs', '-o', 'plaintext', '--url', url()]);
    expect(withoutLogLines(plaintext.stdout)).toBe(withoutLogLines(result.stdout));

    const rowsOut = await cmd(['runs', '-o', 'rows', '--url', url()]);
    expect(lines(rowsOut.stdout).find((l) => l.startsWith(' demo '))).toMatch(
      /^ demo 9 +clangsa \(3\/3\), clang-tidy \(3\/3\), cppcheck \(3\/3\) \d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+ v2 0:00:00 {2}\S/,
    );
  });

  useCase('cmd-runs-history.md', 'Filter and sort runs', async () => {
    expect(Object.keys(await runs(server(), ['-n', 'demo*']))).toEqual(['demo']);
    expect(Object.keys(await runs(server(), ['-n', 'demo', 'multi'])).sort()).toEqual(['demo', 'multi']);

    const sorted = await cmd(['runs', '--sort', 'name', '--order', 'asc', '-o', 'table', '--url', url()]);
    expect(tableRows(sorted.stdout).slice(1).map((r) => r[0])).toEqual(['demo', 'multi']);
    const byDate = await cmd(['runs', '--url', url()]);
    // Default: newest first.
    expect(tableRows(byDate.stdout).slice(1).map((r) => r[0])).toEqual(['multi', 'demo']);

    expect(Object.keys(await runs(server(), ['--all-after-run', 'demo']))).toEqual(['multi']);
    expect(Object.keys(await runs(server(), ['--all-before-run', 'multi']))).toEqual(['demo']);
    expect(Object.keys(await runs(server(), ['--all-after-time', '2020:01:01'])).sort()).toEqual(['demo', 'multi']);
    expect(Object.keys(await runs(server(), ['--all-before-time', '2000:01:01']))).toEqual([]);
    expect(Object.keys(await runs(server(), ['--all-before-time', '2100:01:01:12:00:00'])).sort()).toEqual(['demo', 'multi']);

    // The selectors exclude each other.
    expect(await cmd(['runs', '-n', 'demo', '--all-after-run', 'demo', '--url', url()])).toExitWith(1);
  });

  useCase('cmd-runs-history.md', 'CSV and JSON', async () => {
    const csv = await cmd(['runs', '-o', 'csv', '--url', url()]);
    expect(csv).toExitWith(0);
    const csvLines = lines(csv.stdout);
    expect(csvLines[0]).toBe('Name,Number of unresolved reports,Analyzer statistics,Storage date,Version tag,Duration,Description,CodeChecker version');
    expect(csvLines.find((l) => l.startsWith('demo,'))).toMatch(/^demo,9,clangsa \(3\/3\), clang-tidy \(3\/3\), cppcheck \(3\/3\),[^,]+,v2,0:00:00,,\S/);

    const plain = await runs(server(), ['-n', 'demo']);
    const detailed = await runs(server(), ['-n', 'demo', '--details']);
    expect(plain.demo.runId).toEqual(expect.any(Number));
    expect(plain.demo.runDate).toMatch(/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d/);
    // --details adds data, e.g. the enabled checkers.
    type Stats = Record<string, { enabledCheckers?: string[]; version: string | null }>;
    const plainStats = plain.demo.analyzerStatistics as Stats;
    const detailedStats = detailed.demo.analyzerStatistics as Stats;
    expect(plainStats.clangsa.enabledCheckers).toBeUndefined();
    expect(detailedStats.clangsa.enabledCheckers).toContain('core.DivideZero');
  });

  useCase('cmd-runs-history.md', 'Run history', async () => {
    const result = await cmd(['history', '-n', 'demo', '--url', url()]);
    expect(result).toExitWith(0);
    const rows = tableRows(result.stdout);
    expect(rows[0]).toEqual(['Date', 'Run name', 'Version tag', 'User', 'CodeChecker version', 'Analyzer statistics', 'Description']);
    expect(rows.slice(1).map((r) => [r[1], r[2], r[3], r[5], r[6]])).toEqual([
      ['demo', 'v2', 'Anonymous', 'clangsa (3/3), clang-tidy (3/3), cppcheck (3/3)', ''],
      ['demo', 'v1', 'Anonymous', 'clangsa (3/3), clang-tidy (3/3), cppcheck (3/3)', 'first run'],
    ]);
    const all = await cmd(['history', '--url', url()]);
    expect(tableRows(all.stdout).slice(1).map((r) => r[1]).sort()).toEqual(['demo', 'demo', 'multi']);
  });

  useCase('cmd-runs-history.md', 'Run history', async () => {
    const result = await cmd(['history', '-n', 'no-such-run*', '--url', url()]);
    expect(result).toExitWith(0);
    expect(tableRows(result.stdout).slice(1).map((r) => r[1]).sort()).toEqual(['demo', 'demo', 'multi']);
  }, { name: 'a pattern matching no run prints every run', knownIssue: 20 });
});

describe('usage/cmd-results.md', () => {
  useCase('cmd-results.md', 'List the reports of a run', async () => {
    const result = await cmd(['results', 'demo', '--url', url()]);
    expect(result).toExitWith(0);
    const rows = tableRows(result.stdout);
    expect(rows[0]).toEqual(['File', 'Checker', 'Severity', 'Message', 'Bug path length', 'Analyzer name', 'Review status', 'Detection status']);
    expect(rows.slice(1).map((r) => `${r[0]} | ${r[1]} | ${r[2]} | ${r[5]} | ${r[6]} | ${r[7]}`).sort()).toEqual(
      [
        'main.c @ 11 | core.DivideZero | HIGH | clangsa | UNREVIEWED | UNRESOLVED',
        'main.c @ 21 | core.NullDereference | HIGH | clangsa | UNREVIEWED | NEW',
        'style.cpp @ 4 | core.DivideZero | HIGH | clangsa | UNREVIEWED | NEW',
        'main.c @ 25 | clang-diagnostic-uninitialized | HIGH | clang-tidy | UNREVIEWED | UNRESOLVED',
        'main.c @ 11 | cppcheck-zerodiv | HIGH | cppcheck | UNREVIEWED | UNRESOLVED',
        'main.c @ 21 | cppcheck-nullPointer | HIGH | cppcheck | UNREVIEWED | NEW',
        'main.c @ 25 | cppcheck-uninitvar | HIGH | cppcheck | UNREVIEWED | UNRESOLVED',
        'style.cpp @ 11 | cppcheck-zerodiv | HIGH | cppcheck | UNREVIEWED | UNRESOLVED',
        'style.cpp @ 4 | cppcheck-zerodiv | HIGH | cppcheck | UNREVIEWED | NEW',
      ].sort(),
    );
    // Several runs and wildcards.
    const many = await results(server(), ['demo*', 'multi']);
    expect(new Set(many.map((r) => r.runName))).toEqual(new Set(['demo', 'multi']));
  });

  useCase('cmd-results.md', 'Output formats', async () => {
    const result = await cmd(['results', 'demo', '--tag', 'v1', '--uniqueing', 'on', '-o', 'csv', '--url', url()]);
    expect(result).toExitWith(0);
    expect(lines(result.stdout)[0]).toBe('File,Checker,Severity,Message,Bug path length,Analyzer name,Review status,Detection status');
    expect(lines(result.stdout).slice(1).map((l) => l.split(',').slice(1, 3).join(',')).sort()).toEqual(
      ['clang-diagnostic-uninitialized,HIGH', 'core.DivideZero,HIGH', 'cppcheck-uninitvar,HIGH', 'cppcheck-zerodiv,HIGH', 'cppcheck-zerodiv,HIGH'].sort(),
    );
    for (const format of ['plaintext', 'rows', 'table']) {
      expect(await cmd(['results', 'demo', '-o', format, '--url', url()])).toExitWith(0);
    }
  });

  useCase('cmd-results.md', 'Full details as JSON', async () => {
    const reports = await results(server(), ['demo*', '--details']);
    const divide = reports.find((r) => r.bugHash === '09296549497056a329a9ce22dda7d710')!;
    expect(divide).toMatchObject({
      checkerId: 'core.DivideZero',
      checkedFile: 'main.c',
      checkerMsg: 'Division by zero',
      line: 11,
      column: 12,
      severity: 40,
      reviewData: { status: 0, author: 'Anonymous', isInSource: false },
      detectionStatus: 2,
      fixedAt: null,
      analyzerName: 'clangsa',
      runName: 'demo',
    });
    const details = divide.details as { pathEvents: Array<{ msg: string }>; executionPath: unknown[]; extendedData: unknown[]; comments: unknown[] };
    expect(details.pathEvents.map((e) => e.msg)).toContain("'z' initialized to 0");
    expect(Array.isArray(details.executionPath)).toBe(true);
    expect(divide.blameInfo).toEqual({ commits: null, blame: null });

    const nullptrLow = await results(server(), ['demo'], ['--severity', 'medium']);
    expect(nullptrLow).toEqual([]);
  });

  useCase('cmd-results.md', 'Filters', async () => {
    const hashes = async (filters: string[]) => (await results(server(), ['demo'], filters)).map((r) => r.bugHash.slice(0, 8)).sort();

    expect(await hashes(['--severity', 'high', 'medium', '--checker-name', 'core.*', '--file', '*main.c'])).toEqual(['09296549', '19a15bf4']);
    expect(await hashes(['--checker-msg', '*zero*'])).toEqual(['04d39a25', '09296549', '0983180e', '990560af', 'b8956890']);
    expect(await hashes(['--analyzer-name', 'clang*'])).toEqual(['09296549', '19a15bf4', '2e5ec744', 'b8956890']);
    expect(await hashes(['--analyzer-name', 'clang-tidy'])).toEqual(['2e5ec744']);
    expect(await hashes(['--detection-status', 'resolved'])).toEqual(['249a1217']);
    expect(await hashes(['--review-status', 'false_positive'])).toEqual(['07a8f664', '21b00907', '990560af']);
    expect(await hashes(['--report-status', 'outstanding'])).toEqual(['04d39a25', '09296549', '0983180e', '19a15bf4', '2e5ec744', '3b2c4905', '990560af', 'b8956890', 'dc01f098']);
    expect(await hashes(['--report-hash', '0929654949', '19a15bf46b7d3af4b189b3b4f2c28a5b'])).toEqual(['19a15bf4']);
    expect(await hashes(['--report-hash', '09296549497056a329a9ce22dda7d710'])).toEqual(['09296549']);
    expect(await hashes(['--tag', 'v1'])).toEqual(['04d39a25', '09296549', '2e5ec744', '3b2c4905', '990560af']);
    expect(await hashes(['--detected-after', '2020:01:01'])).toEqual(['04d39a25', '09296549', '0983180e', '19a15bf4', '2e5ec744', '3b2c4905', '990560af', 'b8956890', 'dc01f098']);
    expect(await hashes(['--detected-before', '2000:01:01'])).toEqual([]);
    expect(await hashes(['--outstanding-reports-date', '2100:01:01'])).toEqual(['04d39a25', '09296549', '0983180e', '19a15bf4', '2e5ec744', '3b2c4905', '990560af', 'b8956890', 'dc01f098']);
    expect(await hashes(['--open-reports-date', '2100:01:01'])).toEqual(['04d39a25', '09296549', '0983180e', '19a15bf4', '2e5ec744', '3b2c4905', '990560af', 'b8956890', 'dc01f098']);

    // Fixed reports are resolved, which the default detection filter hides.
    expect(await hashes(['--fixed-after', '2020:01:01'])).toEqual([]);
    expect(await hashes(['--fixed-after', '2020:01:01', '--detection-status', 'resolved'])).toEqual(['249a1217']);
    expect(await hashes(['--fixed-before', '2000:01:01', '--detection-status', 'resolved'])).toEqual([]);

    const bugPath = await results(server(), ['demo'], ['--bug-path-length', '2:']);
    expect(bugPath.every((r) => r.bugPathLength >= 2)).toBe(true);
    const shortPath = await results(server(), ['demo'], ['--bug-path-length', ':1']);
    expect(shortPath.every((r) => r.bugPathLength <= 1)).toBe(true);
    expect(bugPath.length + shortPath.length).toBe(9);

    // One row per bug hash across runs.
    const twoRuns = await results(server(), ['demo', 'multi'], ['--checker-name', 'core.DivideZero']);
    const unique = await results(server(), ['demo', 'multi'], ['--checker-name', 'core.DivideZero', '--uniqueing', 'on']);
    expect(unique.length).toBeLessThan(twoRuns.length);
    expect(new Set(unique.map((r) => r.bugHash)).size).toBe(unique.length);

    const anywhere = await results(server(), ['demo'], ['--file', '*style.cpp', '--anywhere-on-report-path']);
    expect(anywhere.map((r) => r.bugHash.slice(0, 8)).sort()).toEqual(['0983180e', '990560af', 'b8956890']);
  });

  useCase('cmd-results.md', 'Filters', async () => {
    const result = await cmd(['results', 'demo', '--checker-name', 'no-such-checker', '--url', url()]);
    expect(result).toExitWith(0);
    expect(result.stdout.trim()).toBe('');
  }, { name: 'an empty result' });
});

describe('usage/cmd-sum.md', () => {
  useCase('cmd-sum.md', 'Statistics for a run', async () => {
    const result = await cmd(['sum', '-n', 'demo', '--url', url()]);
    expect(result).toExitWith(0);
    const rows = tableRows(result.stdout);
    expect(rows[0]).toEqual(['Checker', 'Severity', 'Unreviewed', 'Confirmed', 'False positive', 'Intentional', 'All reports']);
    const byChecker = Object.fromEntries(rows.filter((r) => r.length === 7).map((r) => [r[0], r.slice(1).join(' ')]));
    expect(byChecker['clang-diagnostic-uninitialized']).toBe('HIGH 1 0 0 0 1');
    expect(byChecker['core.NullDereference']).toBe('HIGH 1 0 0 0 1');
    // "All reports" excludes the false positives that their column counts.
    expect(byChecker['core.DivideZero']).toBe('HIGH 2 0 2 0 2');
    expect(byChecker['cppcheck-nullPointer']).toBe('HIGH 1 0 0 0 1');
    expect(byChecker['cppcheck-uninitvar']).toBe('HIGH 1 0 0 0 1');
    // Three distinct hashes; one of the two style.cpp reports with the same
    // hash is a false positive.
    expect(byChecker['cppcheck-zerodiv']).toBe('HIGH 3 0 1 0 3');
    expect(byChecker.Total).toBe('- 9 0 3 0 9');

    const severities = rows.filter((r) => r.length === 2);
    expect(severities).toEqual([['Severity', 'All reports'], ['HIGH', '9'], ['Total', '9']]);

    const checkerList = rows.filter((r) => r.length === 6);
    expect(checkerList[0]).toEqual(['Checker Name', 'guideline', 'Severity', 'Enabled in all selected runs', 'Closed Reports', 'Outstanding Reports']);
    expect(checkerList.map((r) => r[0])).toEqual(expect.arrayContaining(['__FAKE__', 'NOT FOUND', 'core.DivideZero']));
    expect(checkerList.length).toBeGreaterThan(500);
  }, { knownIssue: 8 });

  useCase('cmd-sum.md', 'All runs, with filters', async () => {
    const json = await cmd(['sum', '--all', '--checker-name', 'core.*', '-o', 'json', '--url', url()]);
    expect(json).toExitWith(0);
    const rows = JSON.parse(json.stdout) as Array<Record<string, unknown>>;
    expect(rows.map((r) => r.checker).sort()).toEqual(['core.DivideZero', 'core.NullDereference']);
    expect(Object.keys(rows[0]).sort()).toEqual(['checker', 'confirmed', 'false_positive', 'intentional', 'reports', 'severity', 'unreviewed']);
    expect(rows.find((r) => r.checker === 'core.NullDereference')).toEqual({
      checker: 'core.NullDereference',
      severity: 'HIGH',
      reports: 1,
      unreviewed: 1,
      confirmed: 0,
      false_positive: 0,
      intentional: 0,
    });

    const high = await cmd(['sum', '--all', '--severity', 'high', '-o', 'json', '--url', url()]);
    expect((JSON.parse(high.stdout) as Array<{ severity: string }>).every((r) => r.severity === 'HIGH')).toBe(true);

    const spaces = await cmd(['sum', '-n', 'demo', 'multi', '-o', 'json', '--url', url()]);
    expect(spaces).toExitWith(0);
    expect((JSON.parse(spaces.stdout) as unknown[]).length).toBeGreaterThan(0);
  });

  useCase('cmd-sum.md', 'All runs, with filters', async () => {
    const result = await cmd(['sum', '-n', 'demo:multi', '--url', url()]);
    expect(logMessages(result.output, 'WARNING')).toContain('No runs were found!');
  }, { name: 'run names separated by colons find nothing', knownIssue: 'H3' });
});
