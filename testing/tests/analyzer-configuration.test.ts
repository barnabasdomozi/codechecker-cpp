// docs/usage/analyzer-configuration.md

import { existsSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, type RunResult } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { DOC_ANALYZERS, logProject, projectCopy, sharedAnalysis, writeCompileDb } from '../src/projects';
import { checkerCounts, parseJson, plainReports, reportRows } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();

/** Folder with Cppcheck's addons/, cfg/ and platforms/, if found. */
function cppcheckDataDir(): string | undefined {
  const candidates = ['/usr/share/cppcheck', '/usr/lib/x86_64-linux-gnu/cppcheck', '/usr/lib/aarch64-linux-gnu/cppcheck', '/usr/lib/cppcheck', '/usr/local/share/cppcheck'];
  return candidates.find((d) => existsSync(path.join(d, 'addons', 'misra.py')) && existsSync(path.join(d, 'cfg', 'posix.cfg')));
}
let demo = '';
let small = '';
let counter = 0;

beforeAll(async () => {
  demo = await projectCopy(box, 'demo');
  await logProject('demo', demo);

  // Small sources whose reports depend on an analyzer option.
  small = box.newDir('small');
  box.write(path.join(small, 'nullret.c'), 'static int *get(void) { return 0; }\nint f(void) { int *p = get(); return *p; }\n');
  box.write(path.join(small, 'hdr.h'), 'inline int *header_null() { return 0; }\n');
  box.write(path.join(small, 'usehdr.cpp'), '#include "hdr.h"\nint *g() { return header_null(); }\n');
  box.write(
    path.join(small, 'verbatim.c'),
    '#ifdef CC_TEST_VERBATIM\nint verbatim(int a) { int z = 0; return a / z; }\n#endif\nint dummy;\n',
  );
  box.write(path.join(small, 'taint.c'), 'int my_source(void);\nvoid my_sink(int);\nvoid f(void) { int v = my_source(); my_sink(v); }\n');
  const gcc = findOnPath('gcc')!;
  const gxx = findOnPath('g++')!;
  writeCompileDb(path.join(small, 'compile_commands.json'), [
    { directory: small, command: `${gcc} -c nullret.c`, file: 'nullret.c' },
    { directory: small, command: `${gxx} -c usehdr.cpp`, file: 'usehdr.cpp' },
    { directory: small, command: `${gcc} -c verbatim.c`, file: 'verbatim.c' },
    { directory: small, command: `${gcc} -c taint.c`, file: 'taint.c' },
  ]);
});

async function analyze(cwd: string, args: string[]): Promise<{ result: RunResult; counts: Record<string, number>; files: Record<string, number> }> {
  counter += 1;
  const out = path.join(cwd, `reports-${counter}`);
  const result = await cc(['analyze', 'compile_commands.json', '-o', out, ...DOC_ANALYZERS, ...args], { cwd });
  if (result.exitCode !== 0) return { result, counts: {}, files: {} };
  const { json } = await parseJson([out], cwd, []);
  const files: Record<string, number> = {};
  for (const r of json.reports) files[path.basename(r.file.path)] = (files[path.basename(r.file.path)] ?? 0) + 1;
  return { result, counts: checkerCounts(json.reports), files };
}

describe('usage/analyzer-configuration.md', () => {
  useCase('analyzer-configuration.md', 'Analyzer options', async () => {
    const both = await analyze(demo, ['--analyzer-config', 'clangsa:mode=shallow', 'clang-tidy:HeaderFilterRegex=.*']);
    expect(both.result).toExitWith(0);

    // A Clang SA option with a visible effect: null pointers returned from
    // inlined functions are reported only when the suppression is off.
    const suppressed = await analyze(small, ['--analyzers', 'clangsa']);
    expect(suppressed.counts['core.NullDereference']).toBeUndefined();
    const reported = await analyze(small, ['--analyzers', 'clangsa', '--analyzer-config', 'clangsa:suppress-null-return-paths=false']);
    expect(reported.result).toExitWith(0);
    expect(reported.counts['core.NullDereference']).toBe(1);
  });

  useCase('analyzer-configuration.md', 'Analyzer options', async () => {
    const tidy = ['--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr'];
    const plain = await analyze(small, tidy);
    expect(plain.files['hdr.h']).toBe(1);
    const filtered = await analyze(small, [...tidy, '--analyzer-config', 'clang-tidy:HeaderFilterRegex=nomatch']);
    expect(filtered.result).toExitWith(0);
    expect(filtered.files['hdr.h']).toBeUndefined();
  }, { name: 'clang-tidy:HeaderFilterRegex' });

  useCase('analyzer-configuration.md', 'Analyzer options', async () => {
    // Cppcheck's data files (addons, libraries, platforms) of this machine.
    const dataDir = cppcheckDataDir();
    expect(dataDir).toBeDefined();
    const options = [
      `cppcheck:addons=${dataDir}/addons/misra.py`,
      `cppcheck:libraries=${dataDir}/cfg/posix.cfg`,
      `cppcheck:platform=${dataDir}/platforms/avr8.xml`,
      'cppcheck:inconclusive=true',
    ];
    counter += 1;
    const result = await cc(
      ['analyze', 'compile_commands.json', '-o', path.join(demo, `reports-${counter}`), '--analyzers', 'cppcheck', '--file', '*/main.c', '--verbose', 'debug_analyzer', '--analyzer-config', ...options],
      { cwd: demo },
    );
    expect(result).toExitWith(0);
    // Each option reaches the Cppcheck command line.
    for (const flag of [`--addon=${dataDir}/addons/misra.py`, `--library=${dataDir}/cfg/posix.cfg`, `--platform=${dataDir}/platforms/avr8.xml`, '--inconclusive']) {
      expect(result.output).toContain(flag);
    }
    const { json } = await parseJson([path.join(demo, `reports-${counter}`)], demo, []);
    expect(checkerCounts(json.reports)).toEqual({ 'cppcheck-uninitvar': 1, 'cppcheck-zerodiv': 1 });
  }, { name: 'cppcheck:addons, libraries, platform, inconclusive', requires: ['command:cppcheck'] });

  useCase('analyzer-configuration.md', 'Checker options', async () => {
    const tidy = ['--analyzers', 'clang-tidy', '-e', 'readability-identifier-length'];
    const plain = await analyze(demo, tidy);
    expect(plain.counts['readability-identifier-length']).toBe(11);
    const configured = await analyze(demo, [
      ...tidy,
      '--checker-config',
      'clang-tidy:readability-identifier-length:MinimumVariableNameLength=1',
    ]);
    expect(configured.result).toExitWith(0);
    expect(configured.counts['readability-identifier-length']).toBe(4);
  });

  useCase('analyzer-configuration.md', 'Checker options', async () => {
    const { result } = await analyze(demo, [
      '--analyzers',
      'clang-tidy',
      '--checker-config',
      'clang-tidy:readability-identifier-length.MinimumVariableNameLength=1',
    ]);
    expect(result).toExitWith(1);
    expect(result.output).toContain(
      'error: argument --checker-config: Checker option in wrong format: clang-tidy:readability-identifier-length.MinimumVariableNameLength=1, should be <analyzer>:<checker>:<option>=<value>',
    );
  }, { name: 'the clang-tidy CHECKER.OPTION form is rejected' });

  useCase('analyzer-configuration.md', 'Pass raw flags from a file', async () => {
    const args = box.write(path.join(demo, 'tidy_args.txt'), '-checks=-*,modernize-use-nullptr\n');
    const { result, counts } = await analyze(demo, [
      '--analyzers',
      'clang-tidy',
      '--analyzer-config',
      `clang-tidy:cc-verbatim-args-file=${args}`,
    ]);
    expect(result).toExitWith(0);
    expect(counts).toEqual({ 'modernize-use-nullptr': 1 });
  });

  useCase('analyzer-configuration.md', 'Pass raw flags from a file', async () => {
    const args = box.write(path.join(small, 'sa.txt'), '-DCC_TEST_VERBATIM\n');
    const without = await analyze(small, ['--analyzers', 'clangsa']);
    expect(without.files['verbatim.c']).toBeUndefined();
    const withFlags = await analyze(small, ['--analyzers', 'clangsa', '--analyzer-config', `clangsa:cc-verbatim-args-file=${args}`]);
    expect(withFlags.result).toExitWith(0);
    expect(withFlags.files['verbatim.c']).toBe(1);
  }, { name: 'Clang SA compiler flags' });

  useCase('analyzer-configuration.md', 'Deprecated forms', async () => {
    const tidyArgs = box.write(path.join(demo, 'tidy_args2.txt'), '-checks=-*,modernize-use-nullptr\n');
    const tidy = await analyze(demo, ['--analyzers', 'clang-tidy', '--tidyargs', tidyArgs]);
    expect(tidy.result).toExitWith(0);
    expect(tidy.counts).toEqual({ 'modernize-use-nullptr': 1 });

    const saArgs = box.write(path.join(small, 'sa2.txt'), '-DCC_TEST_VERBATIM\n');
    const sa = await analyze(small, ['--analyzers', 'clangsa', '--saargs', saArgs]);
    expect(sa.result).toExitWith(0);
    expect(sa.files['verbatim.c']).toBe(1);
  });

  useCase('analyzer-configuration.md', 'Deprecated forms', async () => {
    const cppcheck = ['--analyzers', 'cppcheck'];
    const plain = await analyze(demo, cppcheck);
    expect(plain.counts['cppcheck-zerodiv']).toBeGreaterThan(0);
    // The file holds extra Cppcheck options.
    const args = box.write(path.join(demo, 'cppcheck_args.txt'), '--suppress=zerodiv\n');
    const deprecated = await analyze(demo, [...cppcheck, '--cppcheckargs', args]);
    expect(deprecated.result).toExitWith(0);
    expect(deprecated.counts).toEqual({ 'cppcheck-uninitvar': 1 });
    // The same as cppcheck:cc-verbatim-args-file.
    const verbatim = await analyze(demo, [...cppcheck, '--analyzer-config', `cppcheck:cc-verbatim-args-file=${args}`]);
    expect(verbatim.counts).toEqual(deprecated.counts);
  }, { name: '--cppcheckargs', requires: ['command:cppcheck'] });

  useCase('analyzer-configuration.md', 'Use a .clang-tidy file', async () => {
    const root = await projectCopy(box, 'demo');
    await logProject('demo', root);
    box.write(path.join(root, '.clang-tidy'), 'Checks: "-*,modernize-use-nullptr"\n');
    const { result, counts } = await analyze(root, [
      '--analyzers',
      'clang-tidy',
      '--analyzer-config',
      'clang-tidy:take-config-from-directory=true',
    ]);
    expect(result).toExitWith(0);
    expect(counts).toEqual({ 'modernize-use-nullptr': 1 });

    // -e/-d have no effect on clang-tidy then.
    const withEnable = await analyze(root, [
      '--analyzers',
      'clang-tidy',
      '-e',
      'readability-identifier-length',
      '--analyzer-config',
      'clang-tidy:take-config-from-directory=true',
    ]);
    expect(withEnable.counts).toEqual({ 'modernize-use-nullptr': 1 });

    // Without the option, the file is not used.
    const ignored = await analyze(root, ['--analyzers', 'clang-tidy']);
    expect(ignored.counts['modernize-use-nullptr']).toBeUndefined();
  });

  useCase('analyzer-configuration.md', 'Use a .clang-tidy file', async () => {
    const root = await projectCopy(box, 'demo');
    await logProject('demo', root);
    const dump = await cc(['analyzers', '--dump-config', 'clang-tidy'], { cwd: root });
    box.write(path.join(root, '.clang-tidy'), dump.stdout);
    const { result } = await analyze(root, ['--analyzers', 'clang-tidy', '--analyzer-config', 'clang-tidy:take-config-from-directory=true']);
    expect(result).toExitWith(0);
  }, { name: 'a file generated with --dump-config' });

  useCase('analyzer-configuration.md', 'Z3 refutation', async () => {
    const shared = await sharedAnalysis('demo');
    const root = await projectCopy(box, 'demo');
    await logProject('demo', root);
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--z3-refutation', 'off'], { cwd: root });
    expect(result).toExitWith(0);
    const strip = (rows: Awaited<ReturnType<typeof reportRows>>) => rows.map((r) => `${r.location} ${r.checker} ${r.hash}`);
    expect(strip(await reportRows(path.join(root, 'reports'), root))).toEqual(strip(await reportRows(shared.reports, shared.root)));
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'on', ...DOC_ANALYZERS, '--z3-refutation', 'on'], { cwd: root })).toExitWith(0);
  });

  useCase('analyzer-configuration.md', 'Taint analysis configuration', async () => {
    const config = box.write(
      path.join(small, 'taint.yaml'),
      'Propagations:\n  - Name: my_source\n    DstArgs: [-1]\nSinks:\n  - Name: my_sink\n    Args: [0]\n',
    );
    const enable = ['--analyzers', 'clangsa', '-e', 'alpha.security.taint.GenericTaint'];
    const without = await analyze(small, enable);
    expect(without.result).toExitWith(0);
    expect(without.counts['alpha.security.taint.GenericTaint']).toBeUndefined();

    counter += 1;
    const out = path.join(small, `reports-${counter}`);
    const withConfig = await cc(
      [
        'analyze',
        'compile_commands.json',
        '-o',
        out,
        ...enable,
        '--checker-config',
        `clangsa:alpha.security.taint.TaintPropagation:Config=${config}`,
      ],
      { cwd: small },
    );
    expect(withConfig).toExitWith(0);
    const parsed = await cc(['parse', out], { cwd: small });
    expect(plainReports(parsed.stdout).map((r) => `${path.basename(r.path)}:${r.line}:${r.column} ${r.message} [${r.checker}]`)).toEqual([
      'taint.c:3:37 Untrusted data is passed to a user-defined sink [alpha.security.taint.GenericTaint]',
    ]);
  });
});
