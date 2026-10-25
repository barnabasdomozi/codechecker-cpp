// docs/usage/checkers.md

import { describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines } from '../src/normalize';

const box = useSandbox();

interface CheckerInfo {
  name: string;
  analyzer?: string;
  status?: string;
  description?: string;
  labels?: string[];
}

async function checkers(args: string[]): Promise<string> {
  const result = await cc(['checkers', ...args], { cwd: box.dir });
  expect(result).toExitWith(0);
  return result.stdout;
}

async function checkerNames(args: string[]): Promise<string[]> {
  return JSON.parse(await checkers([...args, '-o', 'json'])) as string[];
}

async function installedAnalyzers(): Promise<string[]> {
  const result = await cc(['analyzers', '-o', 'json'], { cwd: box.dir });
  const info = JSON.parse(result.stdout) as Array<{ name: string; path: string | null }>;
  return info.filter((a) => a.path !== null).map((a) => a.name);
}

/** " value" lines of the label/profile/severity listings. */
function listed(text: string): string[] {
  return lines(text)
    .map((l) => l.trim())
    .filter((l) => l !== '');
}

describe('usage/checkers.md', () => {
  useCase('checkers.md', 'List all checkers', async () => {
    const out = await checkers([]);
    // The custom format: a name line, then indented Status/Analyzer/... lines.
    expect(out).toMatch(/^alpha\.clone\.CloneChecker\n  Status: disabled\n  Analyzer: clangsa\n  Description: .*\n  Labels:\n    doc_url:/m);
    expect(out).toMatch(/^core\.DivideZero\n  Status: enabled\n  Analyzer: clangsa\n/m);
    // Checkers of analyzers that are not installed are not listed.
    for (const analyzer of await installedAnalyzers()) {
      expect(out).toContain(`  Analyzer: ${analyzer}\n`);
    }
  });

  useCase('checkers.md', 'Checkers of some analyzers only', async () => {
    const table = await checkers(['--analyzers', 'clang-tidy', '-o', 'table']);
    expect(table).toMatch(/│ Name +│/);
    expect(table).toMatch(/│ abseil-cleanup-ctad +│/);
    expect(table).not.toContain('core.DivideZero');

    const details = JSON.parse(await checkers(['--analyzers', 'clangsa', 'gcc', '--details', '-o', 'json'])) as CheckerInfo[];
    const analyzers = new Set(details.map((c) => c.analyzer));
    expect([...analyzers].sort()).toEqual(['clangsa', 'gcc']);
  });

  useCase('checkers.md', 'Detailed list', async () => {
    const details = JSON.parse(await checkers(['--details', '--analyzers', 'clangsa', '-o', 'json'])) as CheckerInfo[];
    expect(details[0]).toEqual({
      status: 'disabled',
      name: 'alpha.clone.CloneChecker',
      analyzer: 'clangsa',
      description: '(Enable only for development!) Reports similar pieces of code.',
      labels: expect.arrayContaining(['severity:LOW']),
    });
    const divide = details.find((c) => c.name === 'core.DivideZero')!;
    expect(divide.status).toBe('enabled');
    expect(divide.labels).toEqual(expect.arrayContaining(['profile:default', 'severity:HIGH']));
  });

  useCase('checkers.md', 'Output formats', async () => {
    const json = await checkerNames(['--profile', 'sensitive']);
    expect(json.slice(0, 3)).toEqual(['core.BitwiseShift', 'core.CallAndMessage', 'core.DivideZero']);

    const csv = lines(await checkers(['--profile', 'sensitive', '-o', 'csv']));
    expect(csv[0]).toBe('name');
    expect(csv.slice(1)).toEqual(json);

    const rows = listed(await checkers(['--profile', 'sensitive', '-o', 'rows']));
    expect(rows).toEqual(json);

    const table = await checkers(['--profile', 'sensitive', '-o', 'table']);
    expect(table).toMatch(/│ core\.DivideZero +│/);

    const custom = await checkers(['--profile', 'sensitive', '-o', 'custom']);
    expect(custom).toMatch(/^core\.DivideZero\n  Status: enabled/m);
  });

  useCase('checkers.md', 'Browse and filter by label', async () => {
    const keys = listed(await checkers(['--label']));
    for (const key of ['severity', 'profile', 'guideline', 'doc_url', 'sei-cert-c', 'misra-c', 'cwe-vulnerabilities', 'label-tool-skip']) {
      expect(keys).toContain(key);
    }
    const severities = listed(await checkers(['--label', 'severity']));
    expect(severities.sort()).toEqual(['CRITICAL', 'HIGH', 'LOW', 'MEDIUM', 'STYLE', 'UNSPECIFIED']);

    const high = listed(await checkers(['--label', 'severity:HIGH', '-o', 'rows']));
    expect(high).toContain('core.DivideZero');
    expect(high).not.toContain('deadcode.DeadStores');

    const rule = await checkers(['--label', 'sei-cert-c:str34-c', '-o', 'rows']);
    expect(listed(rule)).toEqual(['bugprone-signed-char-misuse']);
  });

  useCase('checkers.md', 'Profiles', async () => {
    expect(listed(await checkers(['--profile']))).toEqual(['default', 'sensitive', 'security', 'portability', 'extreme']);
    const byProfile = await checkerNames(['--profile', 'sensitive']);
    const byLabel = await checkerNames(['--label', 'profile:sensitive']);
    expect(byProfile).toEqual(byLabel);
    const defaults = await checkerNames(['--profile', 'default']);
    // Every default checker is in the sensitive profile too.
    expect(defaults.filter((c) => !byProfile.includes(c))).toEqual([]);
  });

  useCase('checkers.md', 'Guidelines', async () => {
    const all = await checkers(['--guideline']);
    expect(all).toMatch(/^Guideline: cwe-top-25-2024\nRules: cwe-119, cwe-125, /m);
    expect(all).toMatch(/^Guideline: sei-cert-c\nRules: /m);

    const one = await checkers(['--guideline', 'sei-cert-c:str34-c']);
    expect(one).toMatch(/^bugprone-signed-char-misuse\n  Status: enabled\n  Analyzer: clang-tidy\n/);
    const whole = await checkerNames(['--guideline', 'sei-cert-c']);
    expect(whole).toContain('bugprone-signed-char-misuse');
    expect(whole).toEqual(await checkerNames(['--label', 'guideline:sei-cert-c']));
  });

  useCase('checkers.md', 'Severities', async () => {
    expect(listed(await checkers(['--severity']))).toEqual(['CRITICAL', 'HIGH', 'MEDIUM', 'LOW', 'STYLE', 'UNSPECIFIED']);
    const critical = listed(await checkers(['--severity', 'CRITICAL', '--analyzers', 'clang-tidy', '-o', 'rows']));
    expect(critical).toEqual(['clang-diagnostic-error']);
    expect(await checkerNames(['--severity', 'HIGH'])).toEqual(await checkerNames(['--label', 'severity:HIGH']));
  });

  useCase('checkers.md', 'Checker configuration options', async () => {
    // Exit code 1: GCC (and Cppcheck) have no checker options, and that is
    // logged as an error.
    const all = await cc(['checkers', '--checker-config'], { cwd: box.dir });
    expect(all).toExitWith(1);
    // The message names every analyzer without checker options (Cppcheck
    // too, when it is installed).
    expect(all).toOutput(/Failed to get checker configuration options for '[^']*\bgcc\b[^']*' analyzer\(s\)!/);
    expect(listed(all.stdout)).toContain('clangsa:alpha.clone.CloneChecker:MinimumCloneComplexity');

    const options = listed(await checkers(['--checker-config', '--analyzers', 'clangsa', 'clang-tidy']));
    expect(options.length).toBeGreaterThan(100);
    expect(options).toContain('clangsa:alpha.clone.CloneChecker:MinimumCloneComplexity');
    expect(options).toContain('clangsa:alpha.clone.CloneChecker:ReportNormalClones');
    expect(options).toContain('clang-tidy:readability-identifier-length:MinimumVariableNameLength');
    expect(options.every((o) => /^[\w-]+:[^:]+:[^:]+$/.test(o))).toBe(true);
  });

  useCase('checkers.md', 'Deprecated: -w/--warnings', async () => {
    // Compared as sets of checker blocks: the order of the Cppcheck
    // checkers is not fixed, not even between two plain runs.
    const blocks = (text: string) => text.split('\n\n').map((b) => b.trim()).filter((b) => b !== '').sort();
    const plain = blocks(await checkers([]));
    expect(blocks(await checkers(['-w']))).toEqual(plain);
    expect(blocks(await checkers(['--warnings']))).toEqual(plain);
  });
});
