// docs/usage/analyze.md

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, loggedProject, projectCopy, sharedAnalysis, writeCompileDb } from '../src/projects';
import { parseJson, reportRows } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();

function plistFiles(dir: string): string[] {
  return readdirSync(dir)
    .filter((f) => f.endsWith('.plist'))
    .sort();
}

/** `<source>_<analyzer>` of every result file, e.g. `main.c_clangsa`. */
function resultKinds(dir: string): string[] {
  return plistFiles(dir)
    .map((f) => f.replace(/_[0-9a-f]{32}\.plist$/, ''))
    .sort();
}


/** The demo with two compile commands for main.c, as in usage/analyze.md. */
function duplicateDb(root: string): string {
  const gcc = findOnPath('gcc')!;
  return writeCompileDb(path.join(root, 'dup.json'), [
    { directory: root, command: `${gcc} -c main.c -o main.o`, file: 'main.c' },
    { directory: root, command: `${gcc} -DVARIANT -c main.c -o main.o`, file: 'main.c' },
  ]);
}

describe('usage/analyze.md', () => {
  useCase('analyze.md', 'Analyze a compilation database', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'compile_commands.json', '-o', './reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(0);
    const info = logMessages(result.output, 'INFO');
    expect(info).toContain('No checkers enabled for gcc');
    expect(info).toContain('Starting static analysis ...');
    expect(info).toContain('Analysis finished.');
    expect(info).toContain('To view results in the terminal use the "CodeChecker parse" command.');
    expect(info).toContain('To store results use the "CodeChecker store" command.');
    expect(info.find((l) => l.startsWith('Enabled checker list can be found in '))).toMatch(/reports\/metadata\.json$/);
    const progress = info.filter((l) => /^\[\d\/9\] /.test(l)).map((l) => l.replace(/^\[\d\/9\] /, '')).sort();
    expect(progress).toEqual(
      ['clang-tidy', 'clangsa', 'cppcheck'].flatMap((a) => ['main.c', 'style.cpp', 'util.c'].map((f) => `${a} analyzed ${f} successfully.`)).sort(),
    );
    const summary = info.slice(info.indexOf('----==== Summary ====----'));
    expect(summary.slice(0, 6)).toEqual([
      '----==== Summary ====----',
      'Successfully analyzed',
      '  clangsa: 3',
      '  clang-tidy: 3',
      '  cppcheck: 3',
      'Total analyzed compilation commands: 3',
    ]);
    // Analyzers that are not installed are skipped without a warning.
    expect(logMessages(result.output, 'WARNING')).toEqual([]);
    expect(info.some((l) => /^Analysis length: [\d.]+ sec\.$/.test(l))).toBe(true);
  });

  useCase('analyze.md', 'Analyze a compilation database', async () => {
    const demo = await sharedAnalysis('demo');
    const entries = readdirSync(demo.reports);
    for (const name of ['metadata.json', 'compile_cmd.json', 'unique_compile_commands.json', 'compiler_info.json']) {
      expect(entries).toContain(name);
    }
    for (const dir of ['conf', 'fixit', 'ctu_connections']) {
      expect(statSync(path.join(demo.reports, dir)).isDirectory()).toBe(true);
    }
    expect(resultKinds(demo.reports)).toEqual(
      ['main.c', 'style.cpp', 'util.c'].flatMap((f) => ['clang-tidy', 'clangsa', 'cppcheck'].map((a) => `${f}_${a}`)).sort(),
    );
    // Without failures there is no failed/ folder; success/ and reproducer/
    // only exist with their options.
    for (const dir of ['failed', 'success', 'reproducer']) {
      expect(existsSync(path.join(demo.reports, dir))).toBe(false);
    }
    const compileCmd = JSON.parse(readFileSync(path.join(demo.reports, 'compile_cmd.json'), 'utf8')) as unknown[];
    expect(compileCmd).toEqual(JSON.parse(readFileSync(demo.compileDb, 'utf8')));
  }, { name: 'the report folder' });

  useCase('analyze.md', 'Analyze a compilation database', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'a', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'b', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    // The result file names depend only on the compile commands.
    expect(plistFiles(path.join(root, 'b'))).toEqual(plistFiles(path.join(root, 'a')));
    const first = statSync(path.join(root, 'a', plistFiles(path.join(root, 'a'))[0])).mtimeMs;
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'a', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    expect(plistFiles(path.join(root, 'a'))).toEqual(plistFiles(path.join(root, 'b')));
    expect(statSync(path.join(root, 'a', plistFiles(path.join(root, 'a'))[0])).mtimeMs).toBeGreaterThan(first);
  }, { name: 'result file names are stable and overwritten' });

  useCase('analyze.md', 'Analyze one source file', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'main.c', '-o', './reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(0);
    expect(resultKinds(path.join(root, 'reports'))).toEqual(['main.c_clang-tidy', 'main.c_clangsa', 'main.c_cppcheck']);
  });

  useCase('analyze.md', 'Analyze one source file', async () => {
    const dir = box.newDir('no-database');
    const root = await projectCopy(box, 'demo');
    const source = path.join(dir, 'main.c');
    box.write(source, readFileSync(path.join(root, 'main.c'), 'utf8'));
    const result = await cc(['analyze', 'main.c', '-o', './reports', ...DOC_ANALYZERS], { cwd: dir });
    expect(result).toExitWith(0);
    const warnings = logMessages(result.output, 'WARNING');
    expect(warnings).toContain('No analysis is required.');
    expect(warnings).toContain(
      'There were no compilation commands in the provided compilation database or all of them were skipped.',
    );
  }, { name: 'no compile_commands.json in a parent folder', knownIssue: 14 });

  useCase('analyze.md', 'Analyze a folder', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'lib', '-o', './reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(0);
    expect(resultKinds(path.join(root, 'reports'))).toEqual(['util.c_clang-tidy', 'util.c_clangsa', 'util.c_cppcheck']);
  });

  useCase('analyze.md', 'Run only some analyzers', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'sa', '--analyzers', 'clangsa'], { cwd: root })).toExitWith(0);
    expect(resultKinds(path.join(root, 'sa'))).toEqual(['main.c_clangsa', 'style.cpp_clangsa', 'util.c_clangsa']);

    // GCC has no checkers enabled by default, so only clang-tidy runs.
    const two = await cc(['analyze', 'compile_commands.json', '-o', 'tidy', '--analyzers', 'clang-tidy', 'gcc'], { cwd: root });
    expect(two).toExitWith(0);
    expect(logMessages(two.output, 'INFO')).toContain('No checkers enabled for gcc');
    expect(resultKinds(path.join(root, 'tidy'))).toEqual(['main.c_clang-tidy', 'style.cpp_clang-tidy', 'util.c_clang-tidy']);
  });

  useCase('analyze.md', 'Run only some analyzers', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'cppcheck', '--analyzers', 'cppcheck'], { cwd: root });
    expect(result).toExitWith(0);
    expect(resultKinds(path.join(root, 'cppcheck'))).toEqual(['main.c_cppcheck', 'style.cpp_cppcheck', 'util.c_cppcheck']);
    const { json } = await parseJson(['cppcheck'], root);
    expect(json.reports.every((r) => r.analyzer_name === 'cppcheck')).toBe(true);
    expect(json.reports.map((r) => r.checker_name).sort()).toEqual(['cppcheck-uninitvar', 'cppcheck-zerodiv', 'cppcheck-zerodiv', 'cppcheck-zerodiv']);
  }, { name: 'cppcheck', requires: ['command:cppcheck'] });

  useCase('analyze.md', 'Run only some analyzers', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'missing', '--analyzers', 'infer', 'clangsa'], { cwd: root });
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain('Failed to run command because the given analyzer(s) cannot be found on your machine!');
    expect(logMessages(result.output, 'WARNING').some((w) => w.startsWith("Analyzer 'infer' is enabled but CodeChecker failed to execute analysis with it"))).toBe(true);
  }, { name: 'naming an analyzer that is not installed is an error' });

  useCase('analyze.md', 'Run only some analyzers', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'infer', '--analyzers', 'infer'], { cwd: root })).toExitWith(0);
    expect(resultKinds(path.join(root, 'infer')).every((k) => k.endsWith('_infer'))).toBe(true);
  }, { name: 'infer', requires: ['command:infer'] });

  useCase('analyze.md', 'Analyze a compilation database', async () => {
    // Without --analyzers, every analyzer that is found and has enabled
    // checkers runs. GCC has none by default.
    const info = JSON.parse((await cc(['analyzers', '-o', 'json'], { cwd: box.dir })).stdout) as Array<{ name: string; path: string | null }>;
    const expected = info.filter((a) => a.path !== null && a.name !== 'gcc').map((a) => a.name);
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'all'], { cwd: root })).toExitWith(0);
    const ran = [...new Set(resultKinds(path.join(root, 'all')).map((k) => k.replace(/^.*_/, '')))].sort();
    expect(ran).toEqual(expected.sort());
  }, { name: 'every analyzer that is found runs' });

  useCase('analyze.md', 'Number of parallel jobs', async () => {
    const root = await loggedProject(box, 'demo');
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '-j', '1'], { cwd: root });
    expect(result).toExitWith(0);
    const demo = await sharedAnalysis('demo');
    expect(await reportRows(path.join(root, 'reports'), root)).toEqual(await reportRows(demo.reports, demo.root));
  });

  useCase('analyze.md', 'Hide analyzer output', async () => {
    const root = await loggedProject(box, 'build-failures');
    const loud = await cc(['analyze', 'compile_commands.json', '-o', 'a', '--analyzers', 'clangsa'], { cwd: root });
    expect(loud).toExitWith(3);
    expect(loud.output).toContain("error: expected ';' at end of declaration");
    const quiet = await cc(['analyze', 'compile_commands.json', '-o', 'b', '--analyzers', 'clangsa', '-q'], { cwd: root });
    expect(quiet).toExitWith(3);
    expect(quiet.output).not.toContain("error: expected ';' at end of declaration");
  });

  useCase('analyze.md', 'Clean the output folder first', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    expect(plistFiles(path.join(root, 'reports'))).toHaveLength(9);

    // Without -c, results of files not analyzed this time are kept.
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--file', '*/main.c'], { cwd: root })).toExitWith(0);
    expect(plistFiles(path.join(root, 'reports'))).toHaveLength(9);

    expect(
      await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--file', '*/main.c', '-c'], { cwd: root }),
    ).toExitWith(0);
    expect(resultKinds(path.join(root, 'reports'))).toEqual(['main.c_clang-tidy', 'main.c_clangsa', 'main.c_cppcheck']);
  });

  useCase('analyze.md', 'Name the run', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '-n', 'my_run'], { cwd: root })).toExitWith(0);
    const metadata = JSON.parse(readFileSync(path.join(root, 'reports', 'metadata.json'), 'utf8')) as {
      version: number;
      tools: Array<{ run_name?: string }>;
    };
    expect(metadata.version).toBe(2);
    expect(metadata.tools[0].run_name).toBe('my_run');
  });

  useCase('analyze.md', 'Compile uniqueing', async () => {
    const root = await projectCopy(box, 'demo');
    const db = duplicateDb(root);
    const result = await cc(['analyze', db, '-o', './reports', ...DOC_ANALYZERS, '--compile-uniqueing', 'strict'], { cwd: root });
    expect(result).toExitWith(1);
    const gcc = findOnPath('gcc')!;
    expect(logMessages(result.output, 'ERROR')).toContain(
      `Build Action uniqueing failed as both '${gcc} -c main.c -o main.o' and '${gcc} -DVARIANT -c main.c -o main.o'`,
    );
  }, { name: 'strict' });

  useCase('analyze.md', 'Compile uniqueing', async () => {
    const root = await projectCopy(box, 'demo');
    const db = duplicateDb(root);
    const result = await cc(['analyze', db, '-o', './reports', ...DOC_ANALYZERS, '--compile-uniqueing', '.*VARIANT.*'], { cwd: root });
    expect(result).toExitWith(0);
    const unique = JSON.parse(readFileSync(path.join(root, 'reports', 'unique_compile_commands.json'), 'utf8')) as Array<{
      command: string;
    }>;
    expect(unique).toHaveLength(1);
    expect(unique[0].command).toContain('-DVARIANT');
  }, { name: 'regex' });

  useCase('analyze.md', 'Compile uniqueing', async () => {
    const root = await projectCopy(box, 'demo');
    const db = duplicateDb(root);
    const result = await cc(['analyze', db, '-o', './reports', '--analyzers', 'clangsa'], { cwd: root });
    expect(result).toExitWith(0);
    const unique = JSON.parse(readFileSync(path.join(root, 'reports', 'unique_compile_commands.json'), 'utf8')) as unknown[];
    expect(unique).toHaveLength(2);
    expect(plistFiles(path.join(root, 'reports'))).toHaveLength(2);
  }, { name: 'none (default) analyzes every entry' });

  useCase('analyze.md', 'Report hash method', async () => {
    const root = await loggedProject(box, 'demo');
    const hashes: Record<string, string> = {};
    for (const method of ['default', 'context-free', 'context-free-v2', 'diagnostic-message']) {
      const args = ['analyze', 'compile_commands.json', '-o', method, '--analyzers', 'clangsa'];
      if (method !== 'default') args.push('--report-hash', method);
      expect(await cc(args, { cwd: root })).toExitWith(0);
      const { json } = await parseJson([method], root);
      const report = json.reports.find((r) => r.line === 11 && r.file.path.endsWith('/main.c'))!;
      hashes[method] = report.report_hash;
    }
    expect(hashes.default).toBe('09296549497056a329a9ce22dda7d710');
    // The context-free-v2 hash from usage/config-file.md.
    expect(hashes['context-free-v2']).toBe('d11a6fc279b8946a4bdc0365ff032ae0');
    expect(new Set(Object.values(hashes)).size).toBeGreaterThanOrEqual(3);
    for (const h of Object.values(hashes)) expect(h).toMatch(/^[0-9a-f]{32}$/);
  });

  useCase('analyze.md', 'Exit codes', async () => {
    const root = await loggedProject(box, 'demo');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'ok', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'bad', ...DOC_ANALYZERS, '-e', 'no-such-checker'], { cwd: root })).toExitWith(1);
    expect(await cc(['analyze', 'no-such-file.json', '-o', 'missing'], { cwd: root })).toExitWith(1);
    const failing = await loggedProject(box, 'build-failures');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: failing })).toExitWith(3);
  });
});
