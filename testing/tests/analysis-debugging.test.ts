// docs/usage/analysis-debugging.md

import { existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, logProject, projectCopy } from '../src/projects';

const box = useSandbox();

async function loggedDemo(): Promise<string> {
  const root = await projectCopy(box, 'demo');
  await logProject('demo', root);
  return root;
}

const files = (dir: string) => (existsSync(dir) ? readdirSync(dir).sort() : []);
const kinds = (names: string[], suffix: RegExp) => names.map((n) => n.replace(suffix, '')).sort();

describe('usage/analysis-debugging.md', () => {
  useCase('analysis-debugging.md', 'Keep the analyzers\' output for successful runs', async () => {
    const root = await loggedDemo();
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--capture-analysis-output'], { cwd: root })).toExitWith(0);
    const success = files(path.join(root, 'reports', 'success'));
    expect(success.filter((f) => /^main\.c_clangsa_[0-9a-f]{32}\.plist\.stderr\.txt$/.test(f))).toHaveLength(1);
    expect(success.filter((f) => /^main\.c_clang-tidy_[0-9a-f]{32}\.plist\.stderr\.txt$/.test(f))).toHaveLength(1);
    expect(success.filter((f) => /^main\.c_clang-tidy_[0-9a-f]{32}\.plist\.stdout\.txt$/.test(f))).toHaveLength(1);
  });

  useCase('analysis-debugging.md', 'Generate reproducers', async () => {
    const root = await loggedDemo();
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--generate-reproducer'], { cwd: root })).toExitWith(0);
    const zips = files(path.join(root, 'reports', 'reproducer'));
    expect(kinds(zips, /_[0-9a-f]{32}\.plist\.zip$/)).toEqual(
      ['main.c', 'style.cpp', 'util.c'].flatMap((f) => ['clang-tidy', 'clangsa', 'cppcheck'].map((a) => `${f}_${a}`)).sort(),
    );
    expect(files(path.join(root, 'reports', 'failed'))).toEqual([]);
  });

  useCase('analysis-debugging.md', 'Failed analyses', async () => {
    const dir = box.newDir('brk');
    box.write(path.join(dir, 'broken.c'), 'int broken( {\n');
    expect(await cc(['log', '-b', 'gcc -c broken.c -o broken.o', '-o', 'cc.json'], { cwd: dir })).toExitWith(1);
    const analyzed = await cc(['analyze', 'cc.json', '-o', 'reports', '--analyzers', 'clangsa'], { cwd: dir });
    expect(analyzed).toExitWith(3);
    expect(files(path.join(dir, 'reports', 'failed')).filter((f) => /^broken\.c_clangsa_[0-9a-f]{32}\.plist_compile_error\.zip$/.test(f))).toHaveLength(1);

    const status = await cc(['parse', 'reports', '--status', '--detailed'], { cwd: dir });
    expect(status).toExitWith(0);
    const info = logMessages(status.output, 'INFO');
    const failedAt = info.indexOf('Failed to analyze');
    expect(info[failedAt + 1]).toBe(`  clangsa: ['${dir}/broken.c'] (1)`);
    const missingAt = info.indexOf('Missing analysis results');
    expect(info.slice(missingAt + 1)).toContain(`  clang-tidy: ['${dir}/broken.c'] (1)`);
  });

  useCase('analysis-debugging.md', 'Time limit per analysis', async () => {
    const root = await loggedDemo();
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '--timeout', '60'], { cwd: root })).toExitWith(0);
    expect(readdirSync(path.join(root, 'reports')).filter((f) => f.endsWith('.plist'))).toHaveLength(9);
  });

  useCase('analysis-debugging.md', 'Debug logging', async () => {
    const root = await loggedDemo();
    const debug = await cc(['analyze', 'compile_commands.json', '-o', 'a', '--verbose', 'debug', '--analyzers', 'clangsa'], { cwd: root });
    expect(debug).toExitWith(0);
    // Debug lines show the source file and line of CodeChecker itself.
    expect(debug.output).toMatch(/^\[DEBUG\] \[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\] .* - \S+\.\w+:\d+ /m);

    const analyzer = await cc(['analyze', 'compile_commands.json', '-o', 'b', '--verbose', 'debug_analyzer', '--analyzers', 'clangsa'], { cwd: root });
    expect(analyzer).toExitWith(0);
    expect(analyzer.output).toMatch(/clang\S* --analyze /);

    // --verbose is accepted by every command.
    const parse = await cc(['parse', 'a', '--verbose', 'debug'], { cwd: root });
    expect(parse.output).toContain('[DEBUG]');
    expect(await cc(['checkers', '--profile', '--verbose', 'debug'], { cwd: root })).toExitWith(0);
  });
});
