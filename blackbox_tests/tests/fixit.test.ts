// docs/usage/fixit.md (the `cmd results` pipe is in tests/server/)

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { cc, run } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines, logMessages } from '../src/normalize';
import { loggedProject } from '../src/projects';
import { fixitNames } from '../src/reports';
import { findOnPath } from '../src/requirements';

const box = useSandbox();
const NEEDS = { requires: ['command:clang-apply-replacements' as const] };

/** The demo, analyzed as in the docs' setup. */
async function setup(extra: string[] = []): Promise<string> {
  const root = await loggedProject(box, 'demo');
  const result = await cc(
    ['analyze', 'compile_commands.json', '-o', 'reports', '--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr', '-e', 'modernize-use-trailing-return-type', ...extra],
    { cwd: root },
  );
  expect(result).toExitWith(0);
  return root;
}

const line4 = (root: string) => lines(readFileSync(path.join(root, 'style.cpp'), 'utf8'))[3];

describe('usage/fixit.md', () => {
  useCase('fixit.md', 'List all fixes', async () => {
    const root = await setup();
    const result = await cc(['fixit', 'reports'], { cwd: root });
    expect(result).toExitWith(0);
    const names = fixitNames(result.stdout);
    expect(names).toContain('modernize-use-nullptr');
    expect(names).toContain('modernize-use-trailing-return-type');
    expect(result.stdout).toContain(`MainSourceFile: ${root}/style.cpp`);
    expect(result.stdout).toContain(`- BuildDirectory: ${root}`);
    expect(result.stdout).toContain('Message: use a trailing return type for this function');
    // Listing does not change the sources.
    expect(line4(root)).toBe('  int *p = 0; // clang-tidy: modernize-use-nullptr fixit');
  }, NEEDS);

  useCase('fixit.md', 'Filter by checker or file', async () => {
    const root = await setup();
    const result = await cc(['fixit', 'reports', '--checker-name', '*nullptr', '--file', '*/style.cpp'], { cwd: root });
    expect(result).toExitWith(0);
    expect(fixitNames(result.stdout)).toEqual(['modernize-use-nullptr']);
    expect(result.stdout).toContain(
      [`    - FilePath: ${root}/style.cpp`, "      Length: '1'", "      Offset: '49'", '      ReplacementText: nullptr'].join('\n'),
    );
    const none = await cc(['fixit', 'reports', '--checker-name', '*nullptr', '--file', '*/main.c'], { cwd: root });
    expect(none).toExitWith(0);
    expect(fixitNames(none.stdout)).toEqual([]);
    const several = await cc(['fixit', 'reports', '--checker-name', '*nullptr', '*trailing*'], { cwd: root });
    expect(new Set(fixitNames(several.stdout))).toEqual(new Set(['modernize-use-nullptr', 'modernize-use-trailing-return-type']));
  }, NEEDS);

  useCase('fixit.md', 'Apply fixes', async () => {
    const root = await setup();
    const result = await cc(['fixit', 'reports', '-a', '--checker-name', '*nullptr'], { cwd: root });
    expect(result).toExitWith(0);
    expect(result.output).toContain(`Updated files:\n${root}/style.cpp`);
    expect(line4(root)).toBe('  int *p = nullptr; // clang-tidy: modernize-use-nullptr fixit');

    const again = await cc(['fixit', 'reports', '-a', '--checker-name', '*nullptr'], { cwd: root });
    expect(again).toExitWith(0);
    expect(again.output).toContain(`Skipped files due to modification since last analysis:\n${root}/style.cpp`);
  }, NEEDS);

  useCase('fixit.md', 'Choose fixes one by one', async () => {
    const root = await setup();
    const result = await cc(['fixit', 'reports', '-i', '--checker-name', '*nullptr'], { cwd: root, input: '' });
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain(
      "Interactive mode can't be used in case of JSON data arriving from standard input.",
    );
  }, { ...NEEDS, name: 'standard input is not a terminal' });

  useCase('fixit.md', 'Choose fixes one by one', async () => {
    const root = await setup();
    const command = `'${path.resolve(process.env.CODECHECKER_BIN!)}' fixit reports -i --checker-name '*nullptr'`;
    // `script` gives the command a terminal.
    const no = await run('script', ['-qec', command, '/dev/null'], { cwd: root, input: 'n\n' });
    expect(no).toExitWith(0);
    expect(no.stdout).toContain('y/<Enter> - Yes');
    expect(no.stdout).toContain('Apply: ');
    expect(line4(root)).toBe('  int *p = 0; // clang-tidy: modernize-use-nullptr fixit');

    const yes = await run('script', ['-qec', command, '/dev/null'], { cwd: root, input: 'y\n' });
    expect(yes).toExitWith(0);
    expect(line4(root)).toBe('  int *p = nullptr; // clang-tidy: modernize-use-nullptr fixit');
  }, { requires: ['command:clang-apply-replacements', 'command:script'], name: 'answering the prompt' });

  useCase('fixit.md', 'Fix only selected reports (pipe a report list)', async () => {
    const root = await setup();
    const root2 = await loggedProject(box, 'demo2');
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', '--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr'], { cwd: root2 })).toExitWith(0);
    // The new report in demo2 is the nullptr one on style.cpp:4.
    const diff = await cc(['cmd', 'diff', '-b', path.join(root, 'reports'), '-n', path.join(root2, 'reports'), '-o', 'json', '--new'], { cwd: root2 });
    expect(diff).toExitWith(2);
    const fix = await cc(['fixit', path.join(root2, 'reports')], { cwd: root2, input: diff.stdout });
    expect(fix).toExitWith(1);
    expect(fix.output).toContain("KeyError: 'checkedFile'");
  }, { ...NEEDS, name: 'the cmd diff pipe crashes', knownIssue: 2 });

  useCase('fixit.md', 'Fix only selected reports (pipe a report list)', async () => {
    const root = await setup();
    // An empty diff gives an empty list, and then nothing is filtered.
    const diff = await cc(['cmd', 'diff', '-b', 'reports', '-n', 'reports', '-o', 'json', '--new'], { cwd: root });
    expect(diff).toExitWith(0);
    const fix = await cc(['fixit', 'reports'], { cwd: root, input: diff.stdout });
    expect(fix).toExitWith(0);
    expect(new Set(fixitNames(fix.stdout))).toEqual(new Set(['modernize-use-nullptr', 'modernize-use-trailing-return-type']));
  }, { ...NEEDS, name: 'an empty diff applies no filtering', knownIssue: 2 });

  useCase('fixit.md', 'Exit codes', async () => {
    const root = await setup();
    // A PATH without clang-apply-replacements.
    const tool = findOnPath('clang-apply-replacements')!;
    const pathWithout = (process.env.PATH ?? '')
      .split(path.delimiter)
      .filter((dir) => dir !== path.dirname(tool))
      .join(path.delimiter);
    const result = await cc(['fixit', 'reports'], { cwd: root, env: { PATH: pathWithout } });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'ERROR')).toContain('clang-apply-replacements tool is not found');
  }, { ...NEEDS, name: 'clang-apply-replacements missing', knownIssue: 3 });
});
