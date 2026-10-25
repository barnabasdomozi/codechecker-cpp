// docs/usage/compiler-info.md

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, loggedProject, sharedAnalysis } from '../src/projects';
import { reportRows } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();
let root = '';
let counter = 0;

beforeAll(async () => {
  root = await loggedProject(box, 'demo');
});

interface CompilerInfo {
  compiler_includes: string[];
  compiler_standard: string;
  target: string;
}

/** The implicit include options Clang SA gets, from the debug_analyzer log. */
async function includeOptions(args: string[]): Promise<string[]> {
  counter += 1;
  const result = await cc(
    ['analyze', 'compile_commands.json', '-o', `debug-${counter}`, '--analyzers', 'clangsa', '--file', '*/style.cpp', '--verbose', 'debug_analyzer', ...args],
    { cwd: root },
  );
  expect(result).toExitWith(0);
  return [...new Set(result.output.match(/-(?:idirafter|isystem) \S+/g) ?? [])].sort();
}

describe('usage/compiler-info.md', () => {
  useCase('compiler-info.md', 'Save the detected compiler info', async () => {
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'compiler_info.json', '--dump-compiler-info-file'], {
      cwd: root,
    });
    expect(result).toExitWith(0);
    // -o is the output file, and no analysis runs.
    const info = JSON.parse(readFileSync(path.join(root, 'compiler_info.json'), 'utf8')) as Record<string, CompilerInfo>;
    expect(readdirSync(root).filter((f) => f.endsWith('.plist'))).toEqual([]);
    const gcc = findOnPath('gcc')!;
    const gxx = findOnPath('g++')!;
    const cKey = JSON.stringify([gcc, 'c', []]).replace(/,/g, ', ');
    const cxxKey = JSON.stringify([gxx, 'c++', ['-std=c++17']]).replace(/,/g, ', ');
    expect(Object.keys(info).sort()).toEqual([cKey, cxxKey].sort());
    expect(info[cKey].compiler_standard).toBe('-std=gnu17');
    expect(info[cKey].target).toBe('x86_64-linux-gnu');
    expect(info[cKey].compiler_includes).toContain('/usr/include');
    expect(info[cxxKey].compiler_includes.some((d) => /^\/usr\/include\/c\+\+\/\d+$/.test(d))).toBe(true);
  });

  useCase('compiler-info.md', 'Use a saved compiler info file', async () => {
    expect(
      await cc(['analyze', 'compile_commands.json', '-o', 'saved.json', '--dump-compiler-info-file'], { cwd: root }),
    ).toExitWith(0);
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports-saved', ...DOC_ANALYZERS, '--compiler-info-file', 'saved.json'], {
      cwd: root,
    });
    expect(result).toExitWith(0);
    const shared = await sharedAnalysis('demo');
    const strip = (rows: Awaited<ReturnType<typeof reportRows>>) => rows.map((r) => `${r.location} ${r.checker} ${r.hash}`);
    expect(strip(await reportRows(path.join(root, 'reports-saved'), root))).toEqual(strip(await reportRows(shared.reports, shared.root)));
  });

  useCase('compiler-info.md', 'Use a saved compiler info file', async () => {
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports-x', ...DOC_ANALYZERS, '--compiler-info-file', 'compiler_info_missing.json'], {
      cwd: root,
    });
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain('Compiler info file compiler_info_missing.json does not exist');
    expect(existsSync(path.join(root, 'reports-x'))).toBe(false);
  }, { name: 'the file must exist' });

  useCase('compiler-info.md', 'Keep GCC-only include folders', async () => {
    const plain = await includeOptions([]);
    const kept = await includeOptions(['--keep-gcc-include-fixed', '--keep-gcc-intrin']);
    // GCC's own include folder (with the *intrin.h headers) only appears
    // with --keep-gcc-intrin.
    const gccInclude = (opts: string[]) => opts.filter((o) => /\/gcc\/[^ ]+\/include$/.test(o));
    expect(gccInclude(plain)).toEqual([]);
    expect(gccInclude(kept).length).toBeGreaterThan(0);
  });

  useCase('compiler-info.md', 'Use -isystem instead of -idirafter', async () => {
    const plain = await includeOptions([]);
    expect(plain.length).toBeGreaterThan(0);
    expect(plain.every((o) => o.startsWith('-idirafter '))).toBe(true);
    const isystem = await includeOptions(['--add-gcc-include-dirs-with-isystem']);
    expect(isystem.every((o) => o.startsWith('-isystem '))).toBe(true);
    expect(isystem.map((o) => o.replace('-isystem ', ''))).toEqual(plain.map((o) => o.replace('-idirafter ', '')));
  });
});
