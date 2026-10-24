// docs/usage/parse-export.md

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { sharedAnalysis, type Analysis } from '../src/projects';
import type { JsonReportFile } from '../src/reports';

const box = useSandbox();
let demo: Analysis;

beforeAll(async () => {
  demo = await sharedAnalysis('demo');
});

const parse = (args: string[], env?: Record<string, string | undefined>) => cc(['parse', demo.reports, ...args], { cwd: demo.root, env });

/** Gerrit's changed-files list, as in usage/gerrit-integration.md. */
function changedFiles(files: string[]): string {
  const content: Record<string, unknown> = { '/COMMIT_MSG': { status: 'A', lines_inserted: 7, size_delta: 551, size: 551 } };
  for (const f of files) content[f] = { lines_inserted: 5, lines_deleted: 1, size_delta: 10, size: 700 };
  return box.write(path.join(box.newDir('gerrit'), 'changed_files.json'), JSON.stringify(content));
}

describe('usage/parse-export.md', () => {
  useCase('parse-export.md', 'HTML', async () => {
    const out = path.join(box.newDir('html'), 'html');
    const result = await parse(['-e', 'html', '-o', out]);
    expect(result).toExitWith(2);
    const files = readdirSync(out).sort();
    expect(files).toContain('index.html');
    expect(files).toContain('statistics.html');
    const pages = files.filter((f) => f.endsWith('.plist.html')).map((f) => f.replace(/_[0-9a-f]{32}\.plist\.html$/, ''));
    expect(pages.sort()).toEqual(['main.c_clang-tidy', 'main.c_clangsa', 'main.c_cppcheck', 'style.cpp_cppcheck']);
    const index = readFileSync(path.join(out, 'index.html'), 'utf8');
    expect(index).toContain('core.DivideZero');
    expect(index).toContain('clang-diagnostic-unused-variable');
    const page = readFileSync(path.join(out, files.find((f) => f.startsWith('main.c_clangsa_'))!), 'utf8');
    // The source code is embedded; no server is needed.
    expect(page).toContain('return a / z;');
  });

  useCase('parse-export.md', 'HTML', async () => {
    const result = await parse(['-e', 'html']);
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain(
      'Argument --export not allowed without argument --output when exporting to HTML.',
    );
  }, { name: '-o is required' });

  useCase('parse-export.md', 'JSON', async () => {
    const file = path.join(box.newDir('json'), 'reports.json');
    expect(await parse(['-e', 'json', '-o', file])).toExitWith(2);
    const toStdout = await parse(['-e', 'json']);
    expect(toStdout).toExitWith(2);
    const json = JSON.parse(readFileSync(file, 'utf8')) as JsonReportFile;
    expect(JSON.parse(toStdout.stdout)).toEqual(json);
    expect(json.version).toBe(1);
    expect(json.reports).toHaveLength(6);
    const unused = json.reports.find((r) => r.checker_name === 'clang-diagnostic-unused-variable')!;
    expect(unused).toMatchObject({
      file: { id: `${demo.root}/main.c`, path: `${demo.root}/main.c`, original_path: `${demo.root}/main.c` },
      line: 21,
      column: 7,
      message: "unused variable 'unused'",
      severity: 'MEDIUM',
      report_hash: '249a121710b6a5cb467697f323de0d5f',
      analyzer_name: 'clang-tidy',
      category: 'clang',
      type: null,
      review_status: 'unreviewed',
      notes: [],
      macro_expansions: [],
      annotations: {},
    });
    expect(unused.analyzer_result_file_path).toMatch(new RegExp(`^${demo.reports}/main\\.c_clang-tidy_[0-9a-f]{32}\\.plist$`));
    expect(Array.isArray(unused.bug_path_events)).toBe(true);
    expect(Array.isArray(unused.bug_path_positions)).toBe(true);
  });

  useCase('parse-export.md', 'Code Climate / GitLab', async () => {
    const file = path.join(box.newDir('cc'), 'codeclimate.json');
    expect(await parse(['-e', 'codeclimate', '-o', file])).toExitWith(2);
    const issues = JSON.parse(readFileSync(file, 'utf8')) as Array<Record<string, unknown>>;
    expect(issues).toHaveLength(6);
    expect(issues).toContainEqual({
      type: 'issue',
      check_name: 'clang-diagnostic-unused-variable',
      description: "unused variable 'unused'",
      categories: ['Bug Risk'],
      fingerprint: '249a121710b6a5cb467697f323de0d5f',
      severity: 'minor',
      location: { path: `${demo.root}/main.c`, lines: { begin: 21 } },
    });
  });

  useCase('parse-export.md', 'SARIF', async () => {
    const file = path.join(box.newDir('sarif'), 'results.sarif');
    expect(await parse(['-e', 'sarif', '-o', file])).toExitWith(2);
    const sarif = JSON.parse(readFileSync(file, 'utf8')) as {
      version: string;
      $schema: string;
      runs: Array<{ tool: { driver: { name: string; version: string } }; results: unknown[] }>;
    };
    expect(sarif.version).toBe('2.1.0');
    expect(sarif.$schema).toBe(
      'https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json',
    );
    expect(sarif.runs[0].tool.driver.name).toBe('CodeChecker');
    // All review statuses by default: the two false positives are included.
    const all = sarif.runs.flatMap((r) => r.results);
    expect(all).toHaveLength(9);
  });

  useCase('parse-export.md', 'Baseline', async () => {
    const dir = box.newDir('baseline');
    const file = path.join(dir, 'reports.baseline');
    expect(await parse(['-e', 'baseline', '-o', file])).toExitWith(2);
    const baseline = readFileSync(file, 'utf8');
    expect(baseline.split('\n')).toEqual([
      '04d39a256ef58c412c5acf3f9280fe2e',
      '09296549497056a329a9ce22dda7d710',
      '249a121710b6a5cb467697f323de0d5f',
      '2e5ec744a6a360431ece6fe858d3f172',
      '3b2c4905253f6d0f7409d271d65dbdd6',
      // No line break after the last hash.
      '990560afd081e416b9c7cc4c772c4cff',
    ]);

    const wrong = await parse(['-e', 'baseline', '-o', path.join(dir, 'reports.txt')]);
    expect(wrong).toExitWith(1);
    expect(logMessages(wrong.output, 'ERROR')).toContain("Baseline files must have '.baseline' extensions.");
    expect(existsSync(path.join(dir, 'reports.txt'))).toBe(false);
  });

  useCase('parse-export.md', 'Gerrit', async () => {
    const changed = changedFiles(['main.c']);
    const out = path.join(box.newDir('gerrit-out'), 'gerrit_review.json');
    const env = { CC_REPO_DIR: demo.root, CC_CHANGED_FILES: changed, CC_REPORT_URL: 'http://ci.example.com/run/42' };
    expect(await parse(['-e', 'gerrit', '-o', out], env)).toExitWith(2);
    const review = JSON.parse(readFileSync(out, 'utf8')) as {
      tag: string;
      message: string;
      labels: Record<string, number>;
      comments: Record<string, Array<{ range: Record<string, number>; message: string }>>;
    };
    expect(review.tag).toBe('jenkins');
    // Reports in files that were not changed are counted and listed in the
    // message.
    expect(review.message).toBe(
      "CodeChecker found 6 issue(s) in the code.\n\nThere following reports are introduced in files which are not changed and can't be shown as individual reports:\n[HIGH] style.cpp:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n\n See: http://ci.example.com/run/42",
    );
    expect(review.labels).toEqual({ 'Code-Review': -1, Verified: -1 });
    expect(Object.keys(review.comments)).toEqual(['main.c']);
    expect(review.comments['main.c']).toContainEqual({
      range: { start_line: 21, start_character: 7, end_line: 21, end_character: 7 },
      message: "[MEDIUM] main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]\n  int unused = 5;\n",
    });
    expect(review.comments['main.c']).toHaveLength(5);

    // Without -o the JSON goes to standard output.
    const toStdout = await parse(['-e', 'gerrit'], env);
    expect(toStdout).toExitWith(2);
    expect(JSON.parse(toStdout.stdout)).toEqual(review);
  });

  useCase('parse-export.md', 'Gerrit', async () => {
    const changed = changedFiles(['style.cpp']);
    const result = await parse(['-e', 'gerrit'], { CC_REPO_DIR: demo.root, CC_CHANGED_FILES: changed });
    const review = JSON.parse(result.stdout) as { message: string; comments: Record<string, Array<{ message: string }>> };
    // Only reports in changed files become comments; the others are listed
    // in the message.
    expect(Object.keys(review.comments)).toEqual(['style.cpp']);
    expect(review.comments['style.cpp'].map((c) => c.message.split('\n')[0])).toEqual(['[HIGH] style.cpp:11:12: Division by zero. [cppcheck-zerodiv]']);
    expect(review.message.startsWith('CodeChecker found 6 issue(s) in the code.\n\nThere following reports are introduced in files which are not changed')).toBe(true);
    expect(review.message).not.toContain('See:');
  }, { name: 'only changed files get comments' });

  useCase('parse-export.md', 'Gerrit', async () => {
    const missingFiles = await parse(['-e', 'gerrit'], { CC_REPO_DIR: demo.root, CC_CHANGED_FILES: undefined });
    expect(missingFiles).toExitWith(1);
    expect(logMessages(missingFiles.output, 'ERROR')).toContain(
      "When using gerrit output the 'CC_CHANGED_FILES' environment variable needs to be set to the path of changed files json from Gerrit!",
    );
    const missingBoth = await parse(['-e', 'gerrit'], { CC_REPO_DIR: undefined, CC_CHANGED_FILES: undefined });
    expect(missingBoth).toExitWith(1);
    expect(logMessages(missingBoth.output, 'ERROR')).toHaveLength(2);
  }, { name: 'CC_REPO_DIR and CC_CHANGED_FILES are required' });
});
