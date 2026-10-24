// docs/usage/report-converter.md, with the external-tools test project.
// (Storing converted results is in tests/server/.)

import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, run, tool } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { copyProject, mustRun, projectReadme } from '../src/projects';
import { markdownTable, plainReports, reportRows, type TableRow } from '../src/reports';
import { shared } from '../src/shared';

const box = useSandbox();
let root = '';
let expected: TableRow[] = [];
let counter = 0;

beforeAll(async () => {
  const { value } = await shared('external-tools-output', async (dir) => {
    const copy = await copyProject('external-tools', dir);
    await mustRun(run('make', [], { cwd: copy }));
    return copy;
  });
  root = value;
  expected = markdownTable(projectReadme('external-tools'), 'Expected results');
});

/** Converts into a new folder; returns the folder and the result. */
async function convert(type: string, inputs: string[], extra: string[] = []) {
  counter += 1;
  const out = path.join(box.newDir(`rc-${type}`), `rc_${counter}`);
  const result = await tool('report-converter', ['-t', type, '-o', out, ...extra, ...inputs.map((i) => path.join(root, i))], { cwd: root });
  return { out, result };
}

/** The README rows of one conversion, e.g. "-t asan". */
function expectedFor(conversion: string): Array<Record<string, string>> {
  return expected.filter((r) => r.Conversion === conversion || r.Conversion.startsWith(`${conversion} (`));
}

async function expectConversion(type: string, inputs: string[]): Promise<string> {
  const { out, result } = await convert(type, inputs);
  expect(result).toExitWith(0);
  const rows = (await reportRows(out, root)).map((r) => `${r.location} | ${r.analyzer} | ${r.checker} | ${r.severity} | ${r.hash}`);
  const wanted = expectedFor(`-t ${type}`).map((t) => `${t.Location} | ${t.Analyzer} | ${t.Checker} | ${t.Severity} | ${t['Report hash']}`);
  expect(rows.sort()).toEqual(wanted.sort());
  return out;
}

const NEEDS = { requires: ['tool:report-converter' as const, 'command:clang++' as const] };

describe('usage/report-converter.md', () => {
  useCase('report-converter.md', 'Cppcheck', async () => {
    // Cppcheck's own plist output, made by `make cppcheck` in a copy of the
    // project (the shared copy is read-only).
    const copy = await copyProject('external-tools', box.newDir('cppcheck-input'));
    expect(await run('make', ['cppcheck'], { cwd: copy })).toExitWith(0);
    const out = path.join(copy, 'rc_cppcheck');
    expect(await tool('report-converter', ['-t', 'cppcheck', '-o', out, path.join(copy, 'out', 'cppcheck')], { cwd: copy })).toExitWith(0);
    const rows = (await reportRows(out, copy)).map((r) => `${r.location} | ${r.analyzer} | ${r.checker} | ${r.severity} | ${r.hash}`);
    const wanted = expectedFor('-t cppcheck').map((t) => `${t.Location} | ${t.Analyzer} | ${t.Checker} | ${t.Severity} | ${t['Report hash']}`);
    expect(rows.sort()).toEqual(wanted.sort());
    const example = await tool('report-converter', ['--example', 'cppcheck'], { cwd: copy });
    expect(example.stdout).toContain('CC_LOGGER_ABS_PATH=1 CodeChecker log');
  }, { requires: ['tool:report-converter', 'command:cppcheck'] });

  useCase('report-converter.md', 'Show how to produce input for a type', async () => {
    const tidy = await tool('report-converter', ['--example', 'clang-tidy'], { cwd: box.dir });
    expect(tidy).toExitWith(0);
    expect(tidy.stdout).toContain('report-converter -t clang-tidy -o ./codechecker_clang_tidy_reports');
    expect(tidy.stdout).toContain('CodeChecker store ./codechecker_clang_tidy_reports -n clang-tidy');
    const gcc = await tool('report-converter', ['--example', 'gcc'], { cwd: box.dir });
    expect(gcc.stdout).toContain('report-converter -t gcc -o ./codechecker_gcc_reports my_file.cpp.sarif');
    const asan = await tool('report-converter', ['--example', 'asan'], { cwd: box.dir });
    expect(asan).toExitWith(0);
    expect(asan.stdout).toContain('-fsanitize=address');
  }, { requires: ['tool:report-converter'] });

  useCase('report-converter.md', 'Clang-Tidy', async () => {
    const { out, result } = await convert('clang-tidy', ['out/clang-tidy.out']);
    expect(result).toExitWith(0);
    const plist = readdirSync(out).find((f) => f.endsWith('.plist'))!;
    expect(plist).toMatch(/^analyzer_input\.cpp_clang-tidy_[0-9a-f]{32}\.plist$/);
    expect(logMessages(result.output, 'INFO')).toContain(`Create/modify plist file: '${out}/${plist}'.`);
    expect(logMessages(result.output, 'WARNING')).toContain(
      "Use '--meta' option to provide extra information to the CodeChecker server such as analyzer version and analysis command when storing the results to it. For more information see the --help.",
    );
    await expectConversion('clang-tidy', ['out/clang-tidy.out']);
    await expectConversion('clang-tidy-yaml', ['out/clang-tidy-fixes.yaml']);
  }, NEEDS);

  useCase('report-converter.md', 'GCC static analyzer', async () => {
    await expectConversion('gcc', ['out/analyzer_input.cpp.sarif']);
  }, NEEDS);

  useCase('report-converter.md', 'Sanitizers', async () => {
    const out = await expectConversion('asan', ['out/heap_overflow.asan.out', 'out/use_after_free.asan.out']);
    const parsed = await cc(['parse', out], { cwd: root });
    expect(parsed).toExitWith(2);
    expect(parsed.stdout).toMatch(/\[HIGH\] \S+\/src\/heap_overflow\.cpp:4:16: heap-buffer-overflow on address 0x[0-9a-f]+ at pc 0x[0-9a-f]+ bp 0x[0-9a-f]+ sp 0x[0-9a-f]+ \[AddressSanitizer\.generic-error\]/);
    await expectConversion('ubsan', ['out/undefined.ubsan.out']);
    // lsan is marked as not tested in the docs; it works here.
    await expectConversion('lsan', ['out/leak.lsan.out']);
  }, NEEDS);

  useCase('report-converter.md', 'Pylint', async () => {
    const dir = box.newDir('pylint');
    box.write(path.join(dir, 'bad.py'), 'import os\ndef f():\n    return 1\n');
    const pylint = await run('pylint', ['-f', 'json', 'bad.py'], { cwd: dir });
    box.write(path.join(dir, 'pylint.json'), pylint.stdout);
    const out = path.join(dir, 'rc_pylint');
    expect(await tool('report-converter', ['-t', 'pylint', '-o', out, path.join(dir, 'pylint.json')], { cwd: dir })).toExitWith(0);
    const parsed = await cc(['parse', out], { cwd: dir });
    expect(parsed).toExitWith(2);
    const shown = plainReports(parsed.stdout).map((r) => `[${r.severity}] ${path.basename(r.path)}:${r.line}:${r.column}: ${r.message} [${r.checker}]`);
    expect(shown).toEqual(
      expect.arrayContaining([
        '[LOW] bad.py:1:0: Missing module docstring [missing-module-docstring]',
        '[MEDIUM] bad.py:1:0: Unused import os [unused-import]',
        '[LOW] bad.py:2:0: Missing function or method docstring [missing-function-docstring]',
      ]),
    );
  }, { requires: ['tool:report-converter', 'command:pylint'] });

  useCase('report-converter.md', 'Output format', async () => {
    const { out, result } = await convert('clang-tidy', ['out/clang-tidy.out'], ['-e', 'sarif']);
    expect(result).toExitWith(0);
    const files = readdirSync(out);
    expect(files.filter((f) => f.endsWith('.sarif'))).toHaveLength(1);
    expect(files.filter((f) => f.endsWith('.plist'))).toEqual([]);
    // parse reads the SARIF folder too.
    const rows = await reportRows(out, root);
    expect(rows.map((r) => r.checker).sort()).toEqual(['clang-analyzer-core.DivideZero', 'clang-analyzer-unix.Malloc']);
  }, NEEDS);

  useCase('report-converter.md', 'Metadata, file names, cleaning', async () => {
    const { out, result } = await convert('clang-tidy', ['out/clang-tidy.out'], [
      '--meta',
      'analyzer_command=clang-tidy main.c',
      'analyzer_version=19.1.7',
      '--filename',
      '{source_file}_{analyzer}_custom',
    ]);
    expect(result).toExitWith(0);
    expect(readdirSync(out).sort()).toEqual(['analyzer_input.cpp_clang-tidy_custom.plist', 'metadata.json']);
    expect(JSON.parse(readFileSync(path.join(out, 'metadata.json'), 'utf8'))).toEqual({
      version: 2,
      num_of_report_dir: 1,
      tools: [{ name: 'clang-tidy', version: '19.1.7', command: 'clang-tidy main.c' }],
    });

    // -c deletes the old files first.
    box.write(path.join(out, 'old.plist'), 'stale');
    const again = await tool('report-converter', ['-t', 'clang-tidy', '-o', out, '-c', path.join(root, 'out/clang-tidy.out')], { cwd: root });
    expect(again).toExitWith(0);
    expect(readdirSync(out)).not.toContain('old.plist');

    const verbose = await tool('report-converter', ['-t', 'clang-tidy', '-o', `${out}-v`, '-v', path.join(root, 'out/clang-tidy.out')], { cwd: root });
    expect(verbose).toExitWith(0);
    expect(verbose.output).toContain('clang-analyzer-core.DivideZero');
  }, NEEDS);

  useCase('report-converter.md', 'Input without matching results', async () => {
    const { out, result } = await convert('cppcheck', ['out/clang-tidy.out']);
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toContain(`No 'cppcheck' results can be found in '${path.join(root, 'out/clang-tidy.out')}'.`);
    expect(out).toBeTruthy();
  }, NEEDS);
});
