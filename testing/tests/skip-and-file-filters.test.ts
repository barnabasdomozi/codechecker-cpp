// docs/usage/skip-and-file-filters.md

import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, tool } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, logProject, projectCopy, sharedAnalysis } from '../src/projects';
import { parseJson, plainLocations, plainReports } from '../src/reports';

const box = useSandbox();
let root = '';
let counter = 0;

beforeAll(async () => {
  root = await projectCopy(box, 'demo');
  await logProject('demo', root);
});

async function analyze(args: string[], cwd = root) {
  counter += 1;
  const out = path.join(cwd, `reports-${counter}`);
  const result = await cc(['analyze', 'compile_commands.json', '-o', out, ...DOC_ANALYZERS, ...args], { cwd });
  return { result, out };
}

/** Source files that have result files in a report folder. */
function analyzed(dir: string): string[] {
  return [...new Set(readdirSync(dir).filter((f) => f.endsWith('.plist')).map((f) => f.replace(/_(clangsa|clang-tidy|cppcheck|gcc|infer)_.*$/, '')))].sort();
}

describe('usage/skip-and-file-filters.md', () => {
  useCase('skip-and-file-filters.md', 'Skipfile', async () => {
    const skip = box.write(path.join(root, 'skip.txt'), '-*/lib/*\n');
    for (const flag of ['-i', '--ignore', '--skip']) {
      const { result, out } = await analyze([flag, skip]);
      expect(result).toExitWith(0);
      expect(analyzed(out)).toEqual(['main.c', 'style.cpp']);
      expect(logMessages(result.output, 'INFO')).toContain('Skipped compilation commands: 1');
    }
  });

  useCase('skip-and-file-filters.md', 'Skipfile', async () => {
    const skip = box.write(
      path.join(root, 'commented.txt'),
      '# analyze this one file in lib/\n+*/lib/important.c\n# skip the rest of lib/\n-*/lib/*\n# absolute path\n-/usr/*\n# any path part starting with "test"\n-*/test*\n',
    );
    const { result, out } = await analyze(['-i', skip]);
    expect(result).toExitWith(0);
    expect(analyzed(out)).toEqual(['main.c', 'style.cpp']);
  }, { name: 'comments and several rules' });

  useCase('skip-and-file-filters.md', 'Skipfile', async () => {
    const skip = box.write(path.join(root, 'only-main.txt'), '+*/main.c\n-*\n');
    const { result, out } = await analyze(['-i', skip]);
    expect(result).toExitWith(0);
    expect(analyzed(out)).toEqual(['main.c']);
    expect(logMessages(result.output, 'INFO')).toContain('Skipped compilation commands: 2');
  }, { name: 'include only some files' });

  useCase('skip-and-file-filters.md', 'Skipfile', async () => {
    // The first matching line wins.
    const skip = box.write(path.join(root, 'first-wins.txt'), '-*/main.c\n+*/main.c\n');
    const { out } = await analyze(['-i', skip]);
    expect(analyzed(out)).toEqual(['style.cpp', 'util.c']);
  }, { name: 'the first matching line wins' });

  useCase('skip-and-file-filters.md', 'Drop reports from skipped files', async () => {
    const app = await projectCopy(box, 'cmake-app');
    await logProject('cmake-app', app);
    const skip = box.write(path.join(app, 'skip-header.txt'), '-*/inventory/item.h\n');
    const kept = await analyze(['-i', skip], app);
    expect(kept.result).toExitWith(0);
    const keptJson = await parseJson([kept.out], app);
    expect(keptJson.json.reports.filter((r) => r.file.path.endsWith('/item.h'))).toHaveLength(2);

    const dropped = await analyze(['-i', skip, '--drop-reports-from-skipped-files'], app);
    expect(dropped.result).toExitWith(0);
    const droppedJson = await parseJson([dropped.out], app);
    expect(droppedJson.json.reports.filter((r) => r.file.path.endsWith('/item.h'))).toEqual([]);
    expect(droppedJson.json.reports).toHaveLength(keptJson.json.reports.length - 2);
  });

  useCase('skip-and-file-filters.md', 'Skipfile with parse and check', async () => {
    const demo = await sharedAnalysis('demo');
    const skip = box.write('skip_main.txt', '-*/main.c\n');
    const parsed = await cc(['parse', demo.reports, '-i', skip], { cwd: demo.root });
    // Only reports outside main.c are left: Cppcheck's on style.cpp:11.
    expect(parsed).toExitWith(2);
    expect(plainLocations(plainReports(parsed.stdout))).toEqual(['style.cpp:11:12']);

    const libSkip = box.write(path.join(root, 'skip-lib.txt'), '-*/lib/*\n');
    const checked = await cc(['check', '-l', 'compile_commands.json', '-o', path.join(root, 'check-out'), ...DOC_ANALYZERS, '-i', libSkip], { cwd: root });
    expect(checked).toExitWith(2);
    expect(analyzed(path.join(root, 'check-out'))).toEqual(['main.c', 'style.cpp']);
  });

  useCase('skip-and-file-filters.md', 'Analyze only some files (--file)', async () => {
    const one = await analyze(['--file', '*/style.cpp']);
    expect(one.result).toExitWith(0);
    expect(analyzed(one.out)).toEqual(['style.cpp']);

    const two = await analyze(['--file', path.join(root, 'main.c'), '*/lib/*.c']);
    expect(two.result).toExitWith(0);
    expect(analyzed(two.out)).toEqual(['main.c', 'util.c']);
  });

  useCase('skip-and-file-filters.md', 'Analyze only some files (--file)', async () => {
    const header = path.join(root, 'lib', 'util.h');
    const { result, out } = await analyze(['--file', header]);
    expect(result).toExitWith(0);
    expect(analyzed(out)).toEqual(['main.c', 'util.c']);
    const info = logMessages(result.output, 'INFO');
    expect(info).toContain(`Get dependent source files for '${header}'...`);
    expect(info).toContain(`Get dependent source files for '${header}' done.`);
  }, { name: 'an absolute header path analyzes the files that include it' });

  useCase('skip-and-file-filters.md', 'Analyze only some files (--file)', async () => {
    for (const pattern of ['lib/util.h', '*/lib/util.h']) {
      const { result, out } = await analyze(['--file', pattern]);
      expect(result).toExitWith(0);
      expect(logMessages(result.output, 'WARNING')).toContain(
        'There were no compilation commands in the provided compilation database or all of them were skipped.',
      );
      expect(readdirSync(out).filter((f) => f.endsWith('.plist'))).toEqual([]);
    }
  }, { name: 'relative header paths and header globs analyze nothing (6.30.0)' });

  useCase('skip-and-file-filters.md', 'Filter printed results by file (parse --file)', async () => {
    const demo2 = await sharedAnalysis('demo2');
    const result = await cc(['parse', demo2.reports, '--file', '*/main.c'], { cwd: demo2.root });
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:21:16', 'main.c:21:18', 'main.c:25:10', 'main.c:25:10']);
  });

  useCase('skip-and-file-filters.md', 'Skipfile from Gerrit changed files', async () => {
    const dir = box.newDir('gerrit');
    const changed = box.write(
      path.join(dir, 'changed.json'),
      JSON.stringify({ '/COMMIT_MSG': { status: 'A', lines_inserted: 7, size_delta: 551, size: 551 }, 'main.c': { lines_inserted: 5, lines_deleted: 1, size_delta: 10, size: 700 } }),
    );
    const result = await tool('gerrit_changed_files_to_skipfile.py', [changed, path.join(dir, 'skipfile')], { cwd: dir });
    expect(result).toExitWith(0);
    const skipfile = readFileSync(path.join(dir, 'skipfile'), 'utf8');
    expect(skipfile).toBe('+*/main.c\n-*\n');

    const { out } = await analyze(['-i', path.join(dir, 'skipfile')]);
    expect(analyzed(out)).toEqual(['main.c']);
  }, { requires: ['tool:gerrit_changed_files_to_skipfile.py'] });
});
