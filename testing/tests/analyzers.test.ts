// docs/usage/analyzers.md

import { chmodSync, realpathSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { cc, run } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { escapeRegExp, lines, logMessages } from '../src/normalize';
import { findOnPath } from '../src/requirements';

const box = useSandbox();

const ANALYZERS = ['clangsa', 'clang-tidy', 'cppcheck', 'gcc', 'infer'];

interface AnalyzerInfo {
  name: string;
  path: string | null;
  version: string;
}

async function analyzersJson(env?: Record<string, string | undefined>): Promise<AnalyzerInfo[]> {
  const result = await cc(['analyzers', '-o', 'json'], { cwd: box.dir, env });
  expect(result).toExitWith(0);
  return JSON.parse(result.stdout) as AnalyzerInfo[];
}

/** Whether a tool CodeChecker looks for is installed on this machine. */
function installed(name: string): boolean {
  const binary = { clangsa: 'clang', 'clang-tidy': 'clang-tidy', cppcheck: 'cppcheck', gcc: 'g++', infer: 'infer' }[name];
  return binary !== undefined && findOnPath(binary) !== undefined;
}

function wrapper(dir: string, name: string, target: string): string {
  const file = box.write(path.join(dir, name), `#!/bin/sh\nexec ${target} "$@"\n`);
  chmodSync(file, 0o755);
  return file;
}

describe('usage/analyzers.md', () => {
  useCase('analyzers.md', 'List available analyzers', async () => {
    const result = await cc(['analyzers'], { cwd: box.dir });
    expect(result).toExitWith(0);
    const rows = lines(result.stdout).filter((l) => l.trim() !== '');
    expect(rows.map((l) => l.trim().split(/\s+/)[0])).toEqual(ANALYZERS);
    const warnings = logMessages(result.output, 'WARNING');
    for (const name of ANALYZERS) {
      const row = rows.find((l) => l.trim().startsWith(`${name} `))!;
      if (installed(name)) {
        expect(row).not.toContain('NOT FOUND');
      } else {
        expect(row).toMatch(/NOT FOUND$/);
        expect(warnings).toContain(`Can't analyze with '${name}': Failed to detect analyzer binary!`);
      }
    }
  });

  useCase('analyzers.md', 'List available analyzers', async () => {
    const info = await analyzersJson();
    expect(info.map((a) => a.name)).toEqual(ANALYZERS);
    for (const a of info) {
      if (a.path === null) {
        expect(a.version).toBe('NOT FOUND');
        continue;
      }
      expect(path.isAbsolute(a.path)).toBe(true);
      // The reported version is the one the binary itself prints.
      const out = await run(a.path, ['--version'], { cwd: box.dir });
      expect(out.stdout).toContain(a.version);
    }
    expect(info.find((a) => a.name === 'clangsa')?.path).not.toBeNull();
    expect(info.find((a) => a.name === 'clang-tidy')?.path).not.toBeNull();
  }, { name: 'paths and versions are correct' });

  useCase('analyzers.md', 'Other output formats', async () => {
    const json = await analyzersJson();

    const csv = await cc(['analyzers', '-o', 'csv'], { cwd: box.dir });
    expect(csv).toExitWith(0);
    expect(lines(csv.stdout)).toEqual([
      'name,path,version',
      ...json.map((a) => `${a.name},${a.path ?? ''},${a.version}`),
    ]);

    const table = await cc(['analyzers', '-o', 'table'], { cwd: box.dir });
    expect(table).toExitWith(0);
    expect(table.stdout).toMatch(/│ Name +│ Path +│ Version +│/);
    for (const a of json) {
      expect(table.stdout).toMatch(
        new RegExp(`│ ${escapeRegExp(a.name)} +│ ${escapeRegExp(a.path ?? '')} *│ ${escapeRegExp(a.version)} +│`),
      );
    }

    const plain = await cc(['analyzers'], { cwd: box.dir });
    for (const flag of ['--all', '--details']) {
      const deprecated = await cc(['analyzers', flag], { cwd: box.dir });
      expect(deprecated).toExitWith(0);
      expect(deprecated.stdout).toBe(plain.stdout);
    }
  });

  useCase('analyzers.md', 'Show the configuration options of an analyzer', async () => {
    for (const analyzer of ['clangsa', 'clang-tidy', 'gcc', 'cppcheck']) {
      const result = await cc(['analyzers', '--analyzer-config', analyzer], { cwd: box.dir });
      expect(result).toExitWith(0);
      const options = lines(result.stdout)
        .filter((l) => l.startsWith(' '))
        .map((l) => l.trim().split(' ')[0]);
      expect(options.length).toBeGreaterThan(0);
      expect(options.every((o) => o.startsWith(`${analyzer}:`))).toBe(true);
      // Every analyzer has the CodeChecker-specific verbatim args option.
      expect(options).toContain(`${analyzer}:cc-verbatim-args-file`);
      if (analyzer === 'clang-tidy') {
        expect(options.slice(0, 7)).toEqual(
          ['Checks', 'WarningsAsErrors', 'HeaderFilterRegex', 'ExcludeHeaderFilterRegex', 'FormatStyle', 'User', 'SystemHeaders'].map(
            (o) => `clang-tidy:${o}`,
          ),
        );
        // Listed as the last option.
        expect(options[options.length - 1]).toBe('clang-tidy:take-config-from-directory');
      }
      if (analyzer === 'clangsa') {
        expect(options).toContain('clangsa:add-pop-up-notes');
        expect(options).toContain('clangsa:aggressive-binary-operation-simplification');
      }
      if (analyzer === 'cppcheck') {
        expect(options).toEqual(
          ['addons', 'libraries', 'platform', 'inconclusive', 'cc-verbatim-args-file'].map((o) => `cppcheck:${o}`),
        );
      }
    }
    const json = await cc(['analyzers', '--analyzer-config', 'clang-tidy', '-o', 'json'], { cwd: box.dir });
    expect(json).toExitWith(0);
    const parsed = JSON.parse(json.stdout) as Array<{ option: string; description: string }>;
    expect(parsed[0]).toEqual({ option: 'clang-tidy:Checks', description: "'clang-diagnostic-*,clang-analyzer-*,*'" });
  });

  useCase('analyzers.md', 'Dump a full analyzer configuration', async () => {
    const result = await cc(['analyzers', '--dump-config', 'clang-tidy'], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(result.stdout.startsWith('---\n')).toBe(true);
    expect(result.stdout).toContain("Checks:          'clang-diagnostic-*,clang-analyzer-*,*'");
    expect(result.stdout).toContain('WarningsAsErrors:');
  }, { name: 'clang-tidy' });

  useCase('analyzers.md', 'Dump a full analyzer configuration', async () => {
    const result = await cc(['analyzers', '--dump-config', 'clangsa'], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(result.stdout).toContain('OVERVIEW: Clang Static Analyzer Checker and Package Option List');
  }, { name: 'clangsa prints the Clang option help', knownIssue: 'H2' });

  useCase('analyzers.md', 'Dump a full analyzer configuration', async () => {
    const result = await cc(['analyzers', '--dump-config', 'cppcheck'], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'WARNING')).toContain("'--dump-config cppcheck' is not supported.");
  }, { name: 'cppcheck is not supported', requires: ['command:cppcheck'] });

  useCase('analyzers.md', 'Choose which analyzer binaries are used', async () => {
    const result = await cc(['analyzers'], { cwd: box.dir, env: { CC_ANALYZER_BIN: 'cppcheck:/no/such/cppcheck' } });
    expect(result).toExitWith(1);
    expect(result.output).toContain('TypeError');
  }, { name: 'CC_ANALYZER_BIN with a missing file crashes', knownIssue: 22 });

  useCase('analyzers.md', 'Choose which analyzer binaries are used', async () => {
    const dir = box.newDir('from-path');
    const tidy = findOnPath('clang-tidy');
    expect(tidy).toBeDefined();
    const fake = wrapper(dir, 'clang-tidy', realpathSync(tidy!));
    const info = await analyzersJson({ CC_ANALYZERS_FROM_PATH: '1', PATH: `${dir}${path.delimiter}${process.env.PATH}` });
    expect(info.find((a) => a.name === 'clang-tidy')).toMatchObject({ path: fake });
  }, { name: 'CC_ANALYZERS_FROM_PATH', requires: ['command:clang-tidy'] });

  useCase('analyzers.md', 'Choose which analyzer binaries are used', async () => {
    const dir = box.newDir('analyzer-bin');
    const clang = realpathSync(findOnPath('clang')!);
    const tidy = realpathSync(findOnPath('clang-tidy')!);
    const fakeClang = wrapper(dir, 'my-clang', clang);
    const fakeTidy = wrapper(dir, 'my-clang-tidy', tidy);
    const info = await analyzersJson({ CC_ANALYZER_BIN: `clangsa:${fakeClang};clang-tidy:${fakeTidy}` });
    expect(info.find((a) => a.name === 'clangsa')?.path).toBe(fakeClang);
    expect(info.find((a) => a.name === 'clang-tidy')?.path).toBe(fakeTidy);
  }, { name: 'CC_ANALYZER_BIN', requires: ['command:clang', 'command:clang-tidy'] });
});
