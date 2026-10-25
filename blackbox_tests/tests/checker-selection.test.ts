// docs/usage/checker-selection.md
//
// The docs give the results as report counts per checker from plain
// `CodeChecker parse` (default review-status filter) on the demo project.

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, type RunResult } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, loggedProject } from '../src/projects';
import { checkerCounts, parseJson } from '../src/reports';

const box = useSandbox();
let root = '';
let counter = 0;

beforeAll(async () => {
  root = await loggedProject(box, 'demo');
});

/** Analyzes the demo with the given options; returns the result and per-checker counts. */
async function analyze(args: string[]): Promise<{ result: RunResult; counts: Record<string, number>; out: string }> {
  counter += 1;
  const out = path.join(root, `reports-${counter}`);
  const result = await cc(['analyze', 'compile_commands.json', '-o', out, ...DOC_ANALYZERS, ...args], { cwd: root });
  if (result.exitCode !== 0) return { result, counts: {}, out };
  const { json } = await parseJson([out], root, []);
  return { result, counts: checkerCounts(json.reports), out };
}

describe('usage/checker-selection.md', () => {
  useCase('checker-selection.md', 'Enable one checker', async () => {
    const { result, counts, out } = await analyze(['--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr']);
    expect(result).toExitWith(0);
    expect(counts).toEqual({
      'clang-diagnostic-uninitialized': 1,
      'clang-diagnostic-unused-variable': 1,
      'modernize-use-nullptr': 1,
    });
    // "Where the final list is saved": metadata.json has the enabled checkers.
    const metadata = JSON.parse(readFileSync(path.join(out, 'metadata.json'), 'utf8')) as {
      tools: Array<{ analyzers: Record<string, { checkers: Record<string, boolean> }> }>;
    };
    expect(metadata.tools[0].analyzers['clang-tidy'].checkers['modernize-use-nullptr']).toBe(true);
  });

  useCase('checker-selection.md', 'Disable one checker', async () => {
    const { result, counts } = await analyze(['-e', 'profile:extreme', '-d', 'core.DivideZero']);
    expect(result).toExitWith(0);
    expect(counts['core.DivideZero']).toBeUndefined();
    expect(Object.keys(counts).length).toBeGreaterThan(3);
  });

  useCase('checker-selection.md', 'Run only the checkers you list', async () => {
    const { result, counts } = await analyze(['--disable-all', '-e', 'core.DivideZero']);
    expect(result).toExitWith(0);
    expect(counts).toEqual({ 'core.DivideZero': 1 });
    const same = await analyze(['--disable', 'default', '-e', 'core.DivideZero']);
    expect(same.counts).toEqual(counts);
  });

  useCase('checker-selection.md', 'Enable (almost) every checker', async () => {
    const { result, counts } = await analyze(['--enable-all']);
    expect(result).toExitWith(0);
    expect(counts).toMatchObject({
      'clang-diagnostic-missing-prototypes': 6,
      'clang-diagnostic-uninitialized': 1,
      'readability-identifier-length': 11,
      'readability-magic-numbers': 4,
      'cppcheck-missingIncludeSystem': 3,
      'cppcheck-staticFunction': 3,
      'cppcheck-unassignedVariable': 1,
      'cppcheck-unreadVariable': 1,
      'cppcheck-uninitvar': 1,
      'cppcheck-zerodiv': 2,
    });
    const excluded = ['alpha.', 'debug.', 'osx.', 'abseil-', 'android-', 'darwin-', 'objc-', 'cppcoreguidelines-', 'fuchsia.', 'fuchsia-', 'hicpp-', 'llvm-', 'llvmlibc-', 'google-', 'zircon-'];
    const leaked = Object.keys(counts).filter((c) => excluded.some((p) => c.startsWith(p)));
    expect(leaked).toEqual([]);
  });

  useCase('checker-selection.md', 'Enable a checker prefix group', async () => {
    const { result, counts } = await analyze(['--analyzers', 'clang-tidy', '-e', 'prefix:modernize']);
    expect(result).toExitWith(0);
    expect(counts).toMatchObject({ 'modernize-use-nullptr': 1, 'modernize-use-trailing-return-type': 3 });
    const short = await analyze(['--analyzers', 'clang-tidy', '-e', 'modernize']);
    expect(short.result).toExitWith(0);
    expect(short.counts).toEqual(counts);

    // Clang SA packages work the same way.
    const sa = await analyze(['--analyzers', 'clangsa', '--disable-all', '-e', 'core']);
    expect(sa.result).toExitWith(0);
    expect(sa.counts).toEqual({ 'core.DivideZero': 1 });
  });

  useCase('checker-selection.md', 'Enable a profile', async () => {
    const extreme = await analyze(['-e', 'profile:extreme']);
    expect(extreme.result).toExitWith(0);
    const sensitive = await analyze(['-e', 'sensitive']);
    expect(sensitive.result).toExitWith(0);
    const plain = await analyze([]);
    // Bigger profiles find at least what the default one finds.
    for (const [checker, n] of Object.entries(plain.counts)) {
      expect(extreme.counts[checker]).toBeGreaterThanOrEqual(n);
      expect(sensitive.counts[checker]).toBeGreaterThanOrEqual(n);
    }
    for (const profile of ['default', 'security', 'portability']) {
      expect((await analyze(['-e', `profile:${profile}`])).result).toExitWith(0);
    }
  });

  useCase('checker-selection.md', 'Enable the checkers of a guideline', async () => {
    const { result, counts, out } = await analyze(['-e', 'guideline:sei-cert-c']);
    expect(result).toExitWith(0);
    expect(counts).toEqual({
      'clang-diagnostic-uninitialized': 1,
      'clang-diagnostic-unused-variable': 1,
      'core.DivideZero': 1,
      'cppcheck-uninitvar': 1,
      'cppcheck-zerodiv': 2,
      'gcc-analyzer-use-of-uninitialized-value': 1,
    });
    // A GCC checker was turned on, so GCC ran.
    expect(logMessages(result.output, 'INFO').some((l) => / gcc analyzed /.test(l))).toBe(true);
    expect(out).toBeTruthy();
  });

  useCase('checker-selection.md', 'Enable by any label', async () => {
    const { result, counts } = await analyze(['--analyzers', 'clang-tidy', '-e', 'severity:STYLE']);
    expect(result).toExitWith(0);
    const style = await cc(['checkers', '--label', 'severity:STYLE', '-o', 'json'], { cwd: root });
    const styleCheckers = new Set(JSON.parse(style.stdout) as string[]);
    const extra = Object.keys(counts).filter((c) => !styleCheckers.has(c));
    // Only the default compiler warnings come on top of the STYLE checkers.
    expect(extra.sort()).toEqual(['clang-diagnostic-uninitialized', 'clang-diagnostic-unused-variable']);
    expect(Object.keys(counts).length).toBeGreaterThan(2);
  });

  useCase('checker-selection.md', 'Compiler warnings', async () => {
    const enabled = await analyze(['--analyzers', 'clang-tidy', '-e', 'clang-diagnostic-unused-variable']);
    expect(enabled.result).toExitWith(0);
    expect(enabled.counts['clang-diagnostic-unused-variable']).toBe(1);

    const disabled = await analyze(['--analyzers', 'clang-tidy', '-d', 'clang-diagnostic-unused-variable']);
    expect(disabled.result).toExitWith(0);
    expect(disabled.counts['clang-diagnostic-unused-variable']).toBeUndefined();
    expect(disabled.counts['clang-diagnostic-uninitialized']).toBe(1);
  });

  useCase('checker-selection.md', 'Compiler warnings', async () => {
    const { result } = await analyze(['--analyzers', 'clang-tidy', '-e', 'Wunused-variable']);
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain('No checker(s) with these names was found: Wunused-variable.');
  }, { name: 'the W... form from the help text is rejected', knownIssue: 'H1' });

  useCase('checker-selection.md', 'Unknown checker names', async () => {
    const { result } = await analyze(['-e', 'no-such-checker']);
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain('No checker(s) with these names was found: no-such-checker.');
    expect(logMessages(result.output, 'INFO')).toContain(
      "Although it is not recommended, if you want to suppress errors relating to unknown checker names, consider using the option '--no-missing-checker-error'",
    );

    const relaxed = await analyze(['-e', 'no-such-checker', '--no-missing-checker-error']);
    expect(relaxed.result).toExitWith(0);
    expect(logMessages(relaxed.result.output, 'WARNING').join('\n')).toContain('no-such-checker');
    expect(relaxed.counts).toEqual({
      'clang-diagnostic-uninitialized': 1,
      'clang-diagnostic-unused-variable': 1,
      'core.DivideZero': 1,
      'cppcheck-uninitvar': 1,
      'cppcheck-zerodiv': 2,
    });
  });

  useCase('checker-selection.md', 'Unknown checker names', async () => {
    const { result } = await analyze(['--checker-config', 'clang-tidy:no-such-checker:Option=1']);
    expect(result).toExitWith(1);
    expect(result.output).toContain('no-such-checker');
  }, { name: 'names in --checker-config are checked too' });
});
