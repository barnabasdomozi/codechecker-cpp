// docs/usage/check.md

import { existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages, normalize } from '../src/normalize';
import { DOC_ANALYZERS, logProject, projectCopy, writeCompileDb } from '../src/projects';
import { plainLocations, plainReports } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();

/** The reports plain parse shows for the demo (docs/reference/demo-project.md). */
const DEMO_SHOWN = ['main.c:11:12', 'main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12'];

async function loggedDemo(): Promise<string> {
  const root = await projectCopy(box, 'demo');
  await logProject('demo', root);
  return root;
}

describe('usage/check.md', () => {
  useCase('check.md', 'Build and analyze', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['check', '-b', 'make', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    // Build output, analysis progress, then the parse listing.
    expect(result.output).toContain('gcc -c main.c -o main.o');
    expect(logMessages(result.output, 'INFO')).toContain('Build finished successfully.');
    expect(logMessages(result.output, 'INFO').some((l) => /clangsa analyzed main\.c successfully\./.test(l))).toBe(true);
    expect(plainLocations(plainReports(result.stdout))).toEqual(DEMO_SHOWN);
    expect(result.stdout).toContain('----==== Severity Statistics ====----');
  });

  useCase('check.md', 'Build and analyze', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['check', '-b', 'make', ...DOC_ANALYZERS, '-q'], { cwd: root });
    expect(result).toExitWith(2);
    expect(result.output).not.toContain('gcc -c main.c -o main.o');
    const abs = await cc(['check', '-b', 'make clean all', ...DOC_ANALYZERS, '--use-absolute-ldpreload-path'], { cwd: root });
    expect(abs).toExitWith(2);
    expect(plainReports(abs.stdout)).toHaveLength(6);
  }, { name: '-q and --use-absolute-ldpreload-path apply to the log step' });

  useCase('check.md', 'Use an existing compilation database', async () => {
    const root = await loggedDemo();
    const result = await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(DEMO_SHOWN);
  });

  useCase('check.md', 'Keep the results', async () => {
    const root = await loggedDemo();
    const result = await cc(['check', '-l', 'compile_commands.json', '-o', './reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    expect(readdirSync(path.join(root, 'reports')).filter((f) => f.endsWith('.plist'))).toHaveLength(9);
    expect(await cc(['parse', './reports'], { cwd: root })).toExitWith(2);
  });

  useCase('check.md', 'Keep the results', async () => {
    const root = await loggedDemo();
    const before = readdirSync(box.tmp);
    const result = await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    // The temporary result folder (under TMPDIR) is deleted at the end.
    expect(readdirSync(box.tmp)).toEqual(before);
    expect(existsSync(path.join(root, 'reports'))).toBe(false);
  }, { name: 'without -o, a temporary folder is used and deleted' });

  useCase('check.md', 'Combine analyze and parse options', async () => {
    const root = await loggedDemo();
    const steps = await cc(['check', '-l', 'compile_commands.json', '-o', './reports', ...DOC_ANALYZERS, '--print-steps'], { cwd: root });
    expect(steps).toExitWith(2);
    expect(steps.stdout).toContain("    1, main.c:22:18: Calling 'local_div'");

    const ctu = await cc(
      ['check', '-l', 'compile_commands.json', '--ctu', '--analyzers', 'clangsa', '--trim-path-prefix', `${root}/`],
      { cwd: root },
    );
    expect(ctu).toExitWith(2);
    const lines = ctu.stdout.split('\n').filter((l) => l.startsWith('[HIGH]'));
    expect(lines).toEqual(
      expect.arrayContaining(['[HIGH] main.c:6:12: Division by zero [core.DivideZero]', '[HIGH] main.c:11:12: Division by zero [core.DivideZero]']),
    );
  });

  useCase('check.md', 'Combine analyze and parse options', async () => {
    const root = await loggedDemo();
    const result = await cc(
      [
        'check',
        '-l',
        'compile_commands.json',
        ...DOC_ANALYZERS,
        '-e',
        'modernize-use-nullptr',
        '--review-status',
        'unreviewed',
        'confirmed',
        'false_positive',
        'intentional',
        'suppress',
      ],
      { cwd: root },
    );
    expect(result).toExitWith(2);
    const shown = normalize(result.stdout, { paths: { '~/demo': root } })
      .split('\n')
      .filter((l) => /^\[(HIGH|MEDIUM|LOW)\]/.test(l))
      .sort();
    expect(shown).toEqual(
      [
        '[LOW] ~/demo/style.cpp:4:12: use nullptr [modernize-use-nullptr]',
        "[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]",
        "[HIGH] ~/demo/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized]",
        '[HIGH] ~/demo/style.cpp:11:12: Division by zero [core.DivideZero] [False positive]',
        '[HIGH] ~/demo/style.cpp:17:12: Division by zero [core.DivideZero] [False positive]',
        '[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]',
        '[HIGH] ~/demo/style.cpp:11:12: Division by zero. [cppcheck-zerodiv]',
        '[HIGH] ~/demo/style.cpp:17:12: Division by zero. [cppcheck-zerodiv] [False positive]',
        '[HIGH] ~/demo/main.c:11:12: Division by zero. [cppcheck-zerodiv]',
        '[HIGH] ~/demo/main.c:25:10: Uninitialized variable: x [cppcheck-uninitvar]',
      ].sort(),
    );

    const suppress = box.write(path.join(root, 'suppress.txt'), '09296549497056a329a9ce22dda7d710||main.c||checked||false_positive\n');
    const suppressed = await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS, '--suppress', suppress], { cwd: root });
    expect(suppressed).toExitWith(2);
    expect(plainLocations(plainReports(suppressed.stdout))).toEqual(['main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  }, { name: '--review-status and --suppress' });

  useCase('check.md', 'Exit codes', async () => {
    const clean = await projectCopy(box, 'clean');
    expect(await cc(['check', '-b', 'make', ...DOC_ANALYZERS], { cwd: clean })).toExitWith(0);

    const root = await loggedDemo();
    expect(await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS], { cwd: root })).toExitWith(2);
    expect(await cc(['check', '-l', 'no-such.json'], { cwd: root })).toExitWith(1);
    expect(await cc(['check', '-l', 'compile_commands.json', ...DOC_ANALYZERS, '-e', 'no-such-checker'], { cwd: root })).toExitWith(1);

    // A failed analysis without reports: 3.
    const broken = box.newDir('broken');
    box.write(path.join(broken, 'broken.c'), 'int broken( {\n');
    writeCompileDb(path.join(broken, 'cc.json'), [{ directory: broken, command: `${findOnPath('gcc')} -c broken.c -o broken.o`, file: 'broken.c' }]);
    expect(await cc(['check', '-l', 'cc.json', '--analyzers', 'clangsa'], { cwd: broken })).toExitWith(3);
    // Clang-Tidy reports the compile error, so a report is printed: 2.
    expect(await cc(['check', '-l', 'cc.json', ...DOC_ANALYZERS], { cwd: broken })).toExitWith(2);
  });

  useCase('check.md', 'Exit codes', async () => {
    const root = await projectCopy(box, 'build-failures');
    const result = await cc(['check', '-b', 'make', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(2);
    expect(logMessages(result.output, 'INFO')).toContain('Build failed.');
    // The build failed, so nothing was analyzed.
    expect(logMessages(result.output, 'INFO').some((l) => / analyzed /.test(l))).toBe(false);
    expect(await cc(['check', '-b', 'exit 5'], { cwd: root })).toExitWith(5);
  }, { name: 'a failing build returns its exit code' });
});
