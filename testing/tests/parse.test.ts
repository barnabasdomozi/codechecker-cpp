// docs/usage/parse.md

import { appendFileSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines, logMessages, normalize } from '../src/normalize';
import { analyzeProject, sharedAnalysis, type Analysis } from '../src/projects';
import { plainLocations, plainReports } from '../src/reports';

const box = useSandbox();
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
});

const parse = (args: string[], env?: Record<string, string | undefined>) => cc(['parse', ...args], { cwd: demo.root, env });

/** The tables printed after the reports. */
const STATISTICS = `----==== Severity Statistics ====----
┌──────────┬───────────────────┐
│ Severity │ Number of reports │
├──────────┼───────────────────┤
│ MEDIUM   │ 1                 │
│ HIGH     │ 5                 │
└──────────┴───────────────────┘
----=================----

----==== Checker Statistics ====----
┌──────────────────────────────────┬──────────┬───────────────────┐
│ Checker name                     │ Severity │ Number of reports │
├──────────────────────────────────┼──────────┼───────────────────┤
│ clang-diagnostic-unused-variable │ MEDIUM   │ 1                 │
│ clang-diagnostic-uninitialized   │ HIGH     │ 1                 │
│ cppcheck-zerodiv                 │ HIGH     │ 2                 │
│ core.DivideZero                  │ HIGH     │ 1                 │
│ cppcheck-uninitvar               │ HIGH     │ 1                 │
└──────────────────────────────────┴──────────┴───────────────────┘
----=================----

----==== File Statistics ====----
┌───────────┬───────────────────┐
│ File name │ Number of reports │
├───────────┼───────────────────┤
│ main.c    │ 5                 │
│ style.cpp │ 1                 │
└───────────┴───────────────────┘
----=================----

----======== Summary ========----
┌───────────────────────────────────────────┬───┐
│ Number of processed analyzer result files │ 9 │
├───────────────────────────────────────────┼───┤
│ Number of analyzer reports                │ 6 │
└───────────────────────────────────────────┴───┘
----=================----`;

const STATUS_SUMMARY = [
  '----==== Summary ====----',
  'Up-to-date analysis results',
  '  clangsa: 3',
  '  clang-tidy: 3',
  '  cppcheck: 3',
  'Outdated analysis results',
  'Failed to analyze',
  'Missing analysis results',
  '  gcc: 3',
  '  infer: 3',
  'Total analyzed compilation commands: 3',
  'Total available compilation commands: 3',
  '----=================----',
];

describe('usage/parse.md', () => {
  useCase('parse.md', 'Print a report folder', async () => {
    const result = await parse(['./reports']);
    expect(result).toExitWith(2);
    const out = normalize(result.stdout, { paths: { '~/demo': demo.root } });
    expect(out).toContain(
      "[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]\n  int unused = 5;\n      ^\n",
    );
    expect(out).toContain(
      "[HIGH] ~/demo/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized]\n  return x;\n         ^\n",
    );
    expect(out).toContain('[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]\n  return a / z;\n           ^\n');
    // Reports are grouped by result file.
    expect(out).toContain('[HIGH] ~/demo/main.c:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n           ^\n');
    expect(out).toContain('[HIGH] ~/demo/main.c:25:10: Uninitialized variable: x [cppcheck-uninitvar]\n  return x;\n         ^\n');
    expect(out).toContain('[HIGH] ~/demo/style.cpp:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n           ^\n');
    // Reports are grouped by result file (the order of the groups varies).
    expect(out.split('Found 2 defect(s) in main.c').length - 1).toBe(2);
    expect(out).toContain('Found 1 defect(s) in main.c');
    expect(out).toContain('Found 1 defect(s) in style.cpp');
    expect(out).toContain('Found no defects in style.cpp');
    expect(out).toContain('Found no defects in util.c');
    // The order of the table rows is not fixed: compare the lines as sets.
    const tables = out.slice(out.indexOf('----==== Severity Statistics ====----'));
    expect(lines(tables.slice(0, tables.indexOf('[INFO]') >= 0 ? tables.indexOf('[INFO]') : undefined).trimEnd()).sort()).toEqual(lines(STATISTICS).sort());
    expect(logMessages(result.output, 'INFO').slice(-STATUS_SUMMARY.length)).toEqual(STATUS_SUMMARY);
    expect(plainLocations(plainReports(out))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });

  useCase('parse.md', 'Parse one result file or several folders', async () => {
    const plist = readdirSync(demo.reports).find((f) => f.startsWith('main.c_clangsa_') && f.endsWith('.plist'))!;
    const one = await parse([path.join(demo.reports, plist)]);
    expect(one).toExitWith(2);
    expect(plainLocations(plainReports(one.stdout))).toEqual(['main.c:11:12']);

    const two = await parse([demo.reports, demo2.reports]);
    expect(two).toExitWith(2);
    const reports = plainReports(two.stdout);
    expect(reports.filter((r) => r.path.startsWith(`${demo.root}/`))).toHaveLength(6);
    expect(reports.filter((r) => r.path.startsWith(`${demo2.root}/`))).toHaveLength(9);
  });

  useCase('parse.md', 'Print the bug path', async () => {
    const result = await parse(['./reports', '--print-steps']);
    expect(result).toExitWith(2);
    expect(normalize(result.stdout, { paths: { '~/demo': demo.root } })).toContain(
      [
        '[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]',
        '  return a / z;',
        '           ^',
        '  Report hash: 09296549497056a329a9ce22dda7d710',
        '  Steps:',
        "    1, main.c:22:18: Calling 'local_div'",
        "    2, main.c:9:1: Entered call from 'main'",
        "    3, main.c:10:3: 'z' initialized to 0",
        '    4, main.c:11:12: Division by zero',
        '',
      ].join('\n'),
    );
  });

  useCase('parse.md', 'Analysis status', async () => {
    const result = await parse(['./reports', '--status']);
    expect(result).toExitWith(0);
    expect(plainReports(result.output)).toEqual([]);
    expect(logMessages(result.output, 'INFO')).toEqual(STATUS_SUMMARY);

    const detailed = await parse(['./reports', '--status', '--detailed']);
    expect(detailed).toExitWith(0);
    const files = `['${demo.root}/main.c', '${demo.root}/lib/util.c', '${demo.root}/style.cpp'] (3)`;
    expect(logMessages(detailed.output, 'INFO')).toEqual(
      STATUS_SUMMARY.map((l) => l.replace(/: 3$/, `: ${files}`).replace(/commands: .*$/, (m) => m.replace(files, '3'))),
    );
  });

  useCase('parse.md', 'Analysis status', async () => {
    const result = await parse(['./reports', '--status', '-e', 'json']);
    expect(result).toExitWith(0);
    const status = JSON.parse(result.stdout) as {
      analyzers: Record<string, { summary: Record<string, number>; [k: string]: unknown }>;
      total_analyzed_compilation_commands: number;
      total_available_compilation_commands: number;
    };
    expect(status.analyzers.clangsa).toEqual({ summary: { 'up-to-date': 3, outdated: 0, missing: 0, failed: 0 } });
    expect(status.analyzers['clang-tidy'].summary).toEqual({ 'up-to-date': 3, outdated: 0, missing: 0, failed: 0 });
    expect(status.analyzers.cppcheck.summary).toEqual({ 'up-to-date': 3, outdated: 0, missing: 0, failed: 0 });
    expect(status.analyzers.gcc.summary).toEqual({ 'up-to-date': 0, outdated: 0, missing: 3, failed: 0 });
    expect(status.total_analyzed_compilation_commands).toBe(3);
    expect(status.total_available_compilation_commands).toBe(3);

    const out = path.join(box.newDir('status'), 'status.json');
    const toFile = await parse(['./reports', '--status', '--detailed', '-e', 'json', '-o', out]);
    expect(toFile).toExitWith(0);
    const detailed = JSON.parse(readFileSync(out, 'utf8')) as typeof status;
    expect(detailed.analyzers.clangsa).toEqual({
      summary: { 'up-to-date': 3, outdated: 0, missing: 0, failed: 0 },
      'up-to-date': [`${demo.root}/main.c`, `${demo.root}/lib/util.c`, `${demo.root}/style.cpp`],
      outdated: [],
      missing: [],
      failed: [],
    });
  }, { name: 'JSON export' });

  useCase('parse.md', 'Analysis status', async () => {
    const rejected = await parse(['./reports', '--status', '-e', 'sarif', '-o', path.join(box.dir, 'x.sarif')]);
    expect(rejected).toExitWith(1);
    expect(logMessages(rejected.output, 'ERROR')).toContain('Only JSON export format is supported.');

    const limited = await parse(['./reports', '--status', '--file', 'main.c', '-e', 'json']);
    expect(limited).toExitWith(0);
    const json = JSON.parse(limited.stdout) as { analyzers: Record<string, { summary: Record<string, number> }>; total_analyzed_compilation_commands: number };
    expect(json.analyzers.clangsa.summary['up-to-date']).toBe(1);
    expect(json.total_analyzed_compilation_commands).toBe(1);

    const missing = await parse(['./reports', '--status', '--file', 'no_such.c']);
    expect(missing).toExitWith(1);
    expect(missing.output).toContain('File filter (--file) contains nonexistent files:');
  }, { name: 'other formats are rejected; --file' });

  useCase('parse.md', 'Analysis status', async () => {
    const own = await analyzeProject(box, 'demo');
    appendFileSync(path.join(own.root, 'main.c'), '\n/* changed after the analysis */\n');
    const result = await cc(['parse', own.reports, '--status', '-e', 'json'], { cwd: own.root });
    expect(result).toExitWith(0);
    const json = JSON.parse(result.stdout) as { analyzers: Record<string, { summary: Record<string, number> }> };
    expect(json.analyzers.clangsa.summary).toEqual({ 'up-to-date': 2, outdated: 1, missing: 0, failed: 0 });
    expect(json.analyzers['clang-tidy'].summary).toEqual({ 'up-to-date': 2, outdated: 1, missing: 0, failed: 0 });
    expect(json.analyzers.cppcheck.summary).toEqual({ 'up-to-date': 2, outdated: 1, missing: 0, failed: 0 });
  }, { name: 'a source changed after the analysis is outdated' });

  useCase('parse.md', 'Shorten file paths', async () => {
    const parent = path.dirname(demo.root);
    const result = await parse(['./reports', '--trim-path-prefix', `${parent}/`]);
    expect(result).toExitWith(2);
    expect(result.stdout).toContain("[MEDIUM] demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]");

    // The longest matching prefix is removed; wildcards are allowed.
    const several = await parse(['./reports', '--trim-path-prefix', `${parent}/`, `${demo.root}/`]);
    expect(plainReports(several.stdout).map((r) => r.path).sort()).toEqual(['main.c', 'main.c', 'main.c', 'main.c', 'main.c', 'style.cpp']);
    // '*' also matches '/', and the longest match is removed.
    const wildcard = await parse(['./reports', '--trim-path-prefix', `${path.dirname(parent)}/*/`]);
    expect(plainReports(wildcard.stdout).map((r) => r.path).sort()).toEqual(['main.c', 'main.c', 'main.c', 'main.c', 'main.c', 'style.cpp']);
    const named = await parse(['./reports', '--trim-path-prefix', `${path.dirname(parent)}/*-default/`]);
    expect(plainReports(named.stdout).map((r) => r.path).sort()).toEqual(['demo/main.c', 'demo/main.c', 'demo/main.c', 'demo/main.c', 'demo/main.c', 'demo/style.cpp']);
  });

  useCase('parse.md', 'Show only some files', async () => {
    const byFile = await parse([demo.reports, demo2.reports, '--file', '*/demo2/style.cpp']);
    expect(byFile).toExitWith(2);
    expect(plainLocations(plainReports(byFile.stdout))).toEqual(['style.cpp:11:12', 'style.cpp:4:35', 'style.cpp:4:35']);

    const skip = box.write('skip_main.txt', '-*/main.c\n');
    const skipped = await parse(['./reports', '-i', skip]);
    // Only Cppcheck's unreviewed report in style.cpp is left.
    expect(skipped).toExitWith(2);
    expect(plainLocations(plainReports(skipped.stdout))).toEqual(['style.cpp:11:12']);
    for (const flag of ['--ignore', '--skip']) {
      expect(await parse(['./reports', flag, skip])).toExitWith(2);
    }
  });

  useCase('parse.md', 'Filter by review status', async () => {
    const fp = await parse(['./reports', '--review-status', 'false_positive']);
    expect(fp).toExitWith(2);
    const reportLines = lines(normalize(fp.stdout, { paths: { '~/demo': demo.root } })).filter((l) => /^\[(HIGH|MEDIUM|LOW)\]/.test(l)).sort();
    expect(reportLines).toEqual([
      '[HIGH] ~/demo/style.cpp:11:12: Division by zero [core.DivideZero] [False positive]',
      '[HIGH] ~/demo/style.cpp:17:12: Division by zero [core.DivideZero] [False positive]',
      '[HIGH] ~/demo/style.cpp:17:12: Division by zero. [cppcheck-zerodiv] [False positive]',
    ]);

    const all = await parse(['./reports', '--review-status', 'unreviewed', 'confirmed', 'false_positive', 'intentional', 'suppress']);
    expect(all).toExitWith(2);
    expect(plainReports(all.stdout)).toHaveLength(9);

    const none = await parse(['./reports', '--review-status', 'intentional']);
    expect(none).toExitWith(0);
    expect(plainReports(none.stdout)).toEqual([]);

    // The default is "confirmed unreviewed".
    const explicit = await parse(['./reports', '--review-status', 'confirmed', 'unreviewed']);
    expect(plainReports(explicit.stdout)).toEqual(plainReports((await parse(['./reports'])).stdout));
  });

  useCase('parse.md', 'Suppress files', async () => {
    const dir = box.newDir('suppress');
    const file = path.join(dir, 'suppress.txt');
    const exported = await parse(['./reports', '--export-source-suppress', '--suppress', file]);
    expect(exported).toExitWith(2);
    expect(lines(readFileSync(file, 'utf8')).sort()).toEqual(
      [
        '21b0090745a8a3abdf9a0989d93a8ce7||style.cpp||intentional for the demo||false_positive',
        '07a8f6647771d7458a6a8cf8e4b15643||style.cpp||demo comment||false_positive',
        // Cppcheck's report on style.cpp:17 (hidden by the [all] comment).
        '990560afd081e416b9c7cc4c772c4cff||style.cpp||demo comment||false_positive',
      ].sort(),
    );

    box.write(path.join(dir, 'hide.txt'), '09296549497056a329a9ce22dda7d710||main.c||not a bug||false_positive\n');
    const hidden = await parse(['./reports', '--suppress', path.join(dir, 'hide.txt')]);
    expect(hidden).toExitWith(2);
    expect(plainLocations(plainReports(hidden.stdout))).toEqual(['main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });

  useCase('parse.md', 'Exit codes', async () => {
    expect(await parse(['./reports'])).toExitWith(2);
    expect(await parse(['./reports', '--status'])).toExitWith(0);
    const clean = await sharedAnalysis('clean');
    expect(await cc(['parse', clean.reports], { cwd: clean.root })).toExitWith(0);
    const missing = await parse(['./no-such-folder']);
    expect(missing).toExitWith(1);
    expect(logMessages(missing.output, 'ERROR')).toContain('Input path ./no-such-folder does not exist!');
  });
});
