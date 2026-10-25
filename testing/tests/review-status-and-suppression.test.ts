// docs/usage/review-status-and-suppression.md (sections 1-3; the server
// side is in tests/server/)

import { readdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines, normalize } from '../src/normalize';
import { DOC_ANALYZERS, loggedProject, sharedAnalysis, writeCompileDb } from '../src/projects';
import { plainLocations, plainReports, reportRows } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();
let root = '';
let counter = 0;

beforeAll(async () => {
  root = await loggedProject(box, 'demo');
});

async function analyze(args: string[], cwd = root): Promise<string> {
  counter += 1;
  const out = path.join(cwd, `reports-${counter}`);
  const result = await cc(['analyze', 'compile_commands.json', '-o', out, ...DOC_ANALYZERS, ...args], { cwd });
  expect(result).toExitWith(0);
  return out;
}

const RULE = `$version: 1
rules:
  - filters:
      filepath: "*/main.c"
      checker_name: clang-diagnostic-unused-variable
    actions:
      review_status: intentional
      reason: Kept for demonstration
`;

describe('usage/review-status-and-suppression.md', () => {
  useCase('review-status-and-suppression.md', '1. Source-code comments', async () => {
    const demo = await sharedAnalysis('demo');
    const plain = await cc(['parse', demo.reports], { cwd: demo.root });
    // Only Cppcheck's report on line 11 is shown: its comment names
    // core.DivideZero, not a Cppcheck checker.
    expect(plainReports(plain.stdout).filter((r) => r.path.endsWith('style.cpp')).map((r) => `${r.line} ${r.checker}`)).toEqual(['11 cppcheck-zerodiv']);

    const fp = await cc(['parse', demo.reports, '--review-status', 'false_positive'], { cwd: demo.root });
    expect(fp).toExitWith(2);
    expect(lines(normalize(fp.stdout, { paths: { '~/demo': demo.root } })).filter((l) => l.startsWith('[HIGH]')).sort()).toEqual([
      '[HIGH] ~/demo/style.cpp:11:12: Division by zero [core.DivideZero] [False positive]',
      '[HIGH] ~/demo/style.cpp:17:12: Division by zero [core.DivideZero] [False positive]',
      '[HIGH] ~/demo/style.cpp:17:12: Division by zero. [cppcheck-zerodiv] [False positive]',
    ]);
  });

  useCase('review-status-and-suppression.md', '1. Source-code comments', async () => {
    // The forms tested together in the docs, on one file.
    const dir = box.newDir('forms');
    box.write(
      path.join(dir, 'forms.c'),
      [
        'int f1(int a) { int z = 0;',
        '  // codechecker_intentional [core.DivideZero] keep it',
        '  return a / z; }',
        'int f3(int a) { int z = 0;',
        '  /* codechecker_false_positive [all] c style */',
        '  return a / z; }',
        'int f5(int a) { int z = 0;',
        '  // codechecker_false_positive [core.DivideZero] this is a long',
        '  // comment over two lines',
        '  return a / z; }',
        'int f6(int a) { int z = 0; int u;',
        '  // codechecker_intentional [core.DivideZero] stacked one',
        '  // codechecker_confirmed [clang-diagnostic-uninitialized] stacked two',
        '  return a / z + u; }',
        'int f7(int a) { int z = 0;',
        '  // codechecker_confirmed [core] a prefix matches',
        '  return a / z; }',
        '',
      ].join('\n'),
    );
    writeCompileDb(path.join(dir, 'compile_commands.json'), [
      { directory: dir, command: `${findOnPath('gcc')} -c forms.c -o forms.o`, file: 'forms.c' },
    ]);
    const out = await analyze([], dir);
    const rows = (await reportRows(out, dir)).map((r) => `${r.line} ${r.checker} ${r.reviewStatus}`);
    expect(rows.sort()).toEqual([
      '3 core.DivideZero intentional',
      '3 cppcheck-zerodiv unreviewed',
      '6 core.DivideZero false_positive',
      '6 cppcheck-zerodiv false_positive',
      '10 core.DivideZero false_positive',
      '10 cppcheck-zerodiv unreviewed',
      '14 clang-diagnostic-uninitialized confirmed',
      '14 core.DivideZero intentional',
      '14 cppcheck-uninitvar unreviewed',
      '14 cppcheck-zerodiv unreviewed',
      '17 core.DivideZero confirmed',
      // "core" is not a part of "cppcheck-zerodiv".
      '17 cppcheck-zerodiv unreviewed',
    ].sort());
    // Confirmed reports are shown by default, with a suffix.
    const parsed = await cc(['parse', out], { cwd: dir });
    expect(plainReports(parsed.stdout).filter((r) => r.reviewStatus).map((r) => `${r.line} ${r.checker} ${r.reviewStatus}`).sort()).toEqual(
      ['14 clang-diagnostic-uninitialized Confirmed', '17 core.DivideZero Confirmed'].sort(),
    );
  }, { name: 'every comment form' });

  useCase('review-status-and-suppression.md', '2. Review status rules file (--review-status-config)', async () => {
    const rules = box.write(path.join(root, 'review_status.yaml'), RULE);
    const out = await analyze(['--review-status-config', rules]);
    // analyze copies the file into the report folder.
    expect(readFileSync(path.join(out, 'review_status.yaml'), 'utf8')).toBe(RULE);

    const plain = await cc(['parse', out], { cwd: root });
    expect(plainLocations(plainReports(plain.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);

    const intentional = await cc(['parse', out, '--review-status', 'intentional'], { cwd: root });
    expect(intentional).toExitWith(2);
    expect(lines(normalize(intentional.stdout, { paths: { '~/demo': root } })).filter((l) => l.startsWith('[MEDIUM]'))).toEqual([
      "[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable] [Intentional]",
    ]);
  });

  useCase('review-status-and-suppression.md', '2. Review status rules file (--review-status-config)', async () => {
    const rules = box.write(
      path.join(root, 'by_hash.yaml'),
      '$version: 1\nrules:\n  - filters:\n      report_hash: 09296549\n    actions:\n      review_status: false_positive\n      reason: by hash\n  - filters:\n      checker_name: core.DivideZero\n    actions:\n      review_status: confirmed\n',
    );
    const out = await analyze(['--review-status-config', rules]);
    const rows = (await reportRows(out, root)).map((r) => `${r.location} ${r.checker} ${r.reviewStatus}`);
    expect(rows.sort()).toEqual([
      'main.c:11:12 core.DivideZero false_positive', // rule 1, by hash prefix (the first matching rule wins)
      'main.c:11:12 cppcheck-zerodiv unreviewed',
      'main.c:21:7 clang-diagnostic-unused-variable unreviewed',
      'main.c:25:10 clang-diagnostic-uninitialized unreviewed',
      'main.c:25:10 cppcheck-uninitvar unreviewed',
      'style.cpp:11:12 core.DivideZero false_positive', // the source comment beats rule 2
      'style.cpp:11:12 cppcheck-zerodiv unreviewed',
      'style.cpp:17:12 core.DivideZero false_positive',
      'style.cpp:17:12 cppcheck-zerodiv false_positive',
    ].sort());
    // The result files are not changed: without the copied rules file the
    // statuses are the plain ones again.
    const plist = readdirSync(out).find((f) => f.startsWith('main.c_clangsa_'))!;
    expect(readFileSync(path.join(out, plist), 'utf8')).not.toContain('by hash');
    expect(statSync(path.join(out, 'review_status.yaml')).isFile()).toBe(true);
  }, { name: 'rule by hash; first rule wins; comments win' });

  useCase('review-status-and-suppression.md', '3. Suppress files', async () => {
    const demo = await sharedAnalysis('demo');
    const file = path.join(box.newDir('suppress'), 'suppress.txt');
    expect(await cc(['parse', demo.reports, '--export-source-suppress', '--suppress', file], { cwd: demo.root })).toExitWith(2);
    expect(lines(readFileSync(file, 'utf8')).sort()).toEqual(
      [
        '21b0090745a8a3abdf9a0989d93a8ce7||style.cpp||intentional for the demo||false_positive',
        '07a8f6647771d7458a6a8cf8e4b15643||style.cpp||demo comment||false_positive',
        '990560afd081e416b9c7cc4c772c4cff||style.cpp||demo comment||false_positive',
      ].sort(),
    );
    // Cppcheck's two style.cpp reports share that hash, so the exported file
    // hides the unreviewed one on line 11 too.
    const withExport = await cc(['parse', demo.reports, '--suppress', file], { cwd: demo.root });
    expect(plainLocations(plainReports(withExport.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10']);

    const hide = box.write('hide.txt', '249a121710b6a5cb467697f323de0d5f||main.c||not interesting||false_positive\n');
    const parsed = await cc(['parse', demo.reports, '--suppress', hide], { cwd: demo.root });
    expect(plainLocations(plainReports(parsed.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
    const checked = await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS, '--suppress', hide], { cwd: root });
    expect(checked).toExitWith(2);
    expect(plainLocations(plainReports(checked.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });
});
