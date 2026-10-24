// docs/usage/tu-collector.md, merge-clang-extdef-mappings.md,
// memory-safety-reporter.md, bazel-compile-commands.md,
// statistical-analysis.md

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, run, tool } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { lines, logMessages } from '../src/normalize';
import { logProject, projectCopy, sharedAnalysis } from '../src/projects';
import { isAvailable } from '../src/requirements';
import { zipEntry, zipNames } from '../src/zip';

const box = useSandbox();
let root = '';

beforeAll(async () => {
  root = await projectCopy(box, 'demo');
  await logProject('demo', root);
});

/** The project's own files in a tu_collector zip, relative to the project. */
function projectFiles(zip: string, projectRoot = root): string[] {
  const prefix = `sources-root${projectRoot}/`;
  return zipNames(zip)
    .filter((n) => n.startsWith(prefix))
    .map((n) => n.slice(prefix.length))
    .sort();
}

describe('usage/tu-collector.md', () => {
  const NEEDS = { requires: ['tool:tu_collector' as const] };
  /** For the tests that read the zip. */
  const ZIP_NEEDS = { requires: ['tool:tu_collector' as const, 'command:unzip' as const] };

  useCase('tu-collector.md', 'Zip all translation units', async () => {
    const zip = path.join(root, 'tu.zip');
    const result = await tool('tu_collector', ['-l', 'compile_commands.json', '-z', zip], { cwd: root });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toContain('Done.');
    expect(projectFiles(zip)).toEqual(['lib/util.c', 'lib/util.h', 'main.c', 'style.cpp']);
    const names = zipNames(zip);
    expect(names).toContain('compilation_database.json');
    expect(names).toContain('sources-root/usr/include/stdio.h');
    expect(names.some((n) => /^sources-root\/usr\/include\/c\+\+\/\d+\/cstddef$/.test(n))).toBe(true);
    const db = JSON.parse(zipEntry(zip, 'compilation_database.json').toString('utf8')) as unknown[];
    expect(db).toHaveLength(3);
  }, ZIP_NEEDS);

  useCase('tu-collector.md', 'Zip only some translation units (-f)', async () => {
    for (const filter of ['main.c', '*main.c']) {
      const zip = path.join(root, `tu_main_${filter.replace('*', 'star')}.zip`);
      expect(await tool('tu_collector', ['-l', 'compile_commands.json', '-z', zip, '-f', filter], { cwd: root })).toExitWith(0);
      expect(projectFiles(zip)).toEqual(['lib/util.h', 'main.c']);
      expect(zipNames(zip)).toContain('compilation_database.json');
    }
  }, ZIP_NEEDS);

  useCase('tu-collector.md', 'Zip only some translation units (-f)', async () => {
    for (const filter of [path.join(root, 'main.c'), '*/demo/main.c']) {
      const zip = path.join(root, `abs_${filter.length}.zip`);
      expect(await tool('tu_collector', ['-l', 'compile_commands.json', '-z', zip, '-f', filter], { cwd: root })).toExitWith(0);
      expect(zipNames(zip)).toEqual(['compilation_database.json']);
    }
  }, { ...ZIP_NEEDS, name: 'absolute patterns match nothing', knownIssue: 'H5' });

  useCase('tu-collector.md', 'Include CTU dependencies', async () => {
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'ctu-reports', '--ctu-collect', '--analyzers', 'clangsa'], { cwd: root })).toExitWith(0);
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'ctu-reports', '--ctu-analyze', '--analyzers', 'clangsa'], { cwd: root })).toExitWith(0);
    const zip = path.join(root, 'tu_ctu.zip');
    const result = await tool(
      'tu_collector',
      ['-l', 'compile_commands.json', '-z', zip, '-f', 'main.c', '--ctu-deps-dir', 'ctu-reports/ctu_connections'],
      { cwd: root },
    );
    expect(result).toExitWith(0);
    expect(projectFiles(zip)).toEqual(['lib/util.c', 'lib/util.h', 'main.c']);
  }, ZIP_NEEDS);

  useCase('tu-collector.md', 'Zip one compiler command (-b)', async () => {
    const zip = path.join(root, 't2.zip');
    expect(await tool('tu_collector', ['-b', 'gcc -c main.c -o /dev/null', '-z', zip], { cwd: root })).toExitWith(0);
    expect(projectFiles(zip)).toEqual(['lib/util.h', 'main.c']);

    // The warning says -f has no effect with -b, but then no sources are
    // collected at all (usage/tu-collector.md).
    const filtered = await tool('tu_collector', ['-b', 'gcc -c main.c -o /dev/null', '-z', path.join(root, 't3.zip'), '-f', 'main.c'], { cwd: root });
    expect(filtered).toExitWith(0);
    expect(logMessages(filtered.output, 'WARNING')).toContain('In case of using build command --filter has no effect.');
    expect(zipNames(path.join(root, 't3.zip'))).toEqual(['compilation_database.json']);
  }, ZIP_NEEDS);

  useCase('tu-collector.md', 'Zip one compiler command (-b)', async () => {
    const zip = path.join(root, 'make.zip');
    const result = await tool('tu_collector', ['-b', 'make', '-z', zip], { cwd: root });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'ERROR').map((l) => l.trim())).toContain("Couldn't create dependencies:");
    expect(zipNames(zip)).toEqual(['compilation_database.json']);
  }, { ...ZIP_NEEDS, name: 'a build system command does not work', knownIssue: 10 });

  useCase('tu-collector.md', 'List the sources that include a header (-d)', async () => {
    const result = await tool('tu_collector', ['-l', 'compile_commands.json', '-d', '-f', '*/lib/util.h'], { cwd: root });
    expect(result).toExitWith(0);
    expect(lines(result.stdout).sort()).toEqual([`${root}/lib/util.c`, `${root}/main.c`]);
  }, NEEDS);

  useCase('tu-collector.md', 'Exit code', async () => {
    expect(await tool('tu_collector', ['-l', 'no-such.json', '-z', path.join(root, 'x.zip')], { cwd: root })).toExitWith(1);
    expect(await tool('tu_collector', [], { cwd: root })).toExitWith(2);
  }, NEEDS);
});

describe('usage/merge-clang-extdef-mappings.md', () => {
  const NEEDS = { requires: ['tool:merge-clang-extdef-mappings' as const, 'command:clang-extdef-mapping' as const] };

  async function mappings(): Promise<string> {
    const dir = path.join(root, 'extdef');
    for (const [source, out] of [['main.c', 'main.txt'], ['lib/util.c', 'util.txt']]) {
      const result = await run('clang-extdef-mapping', ['-p', '.', source], { cwd: root });
      expect(result).toExitWith(0);
      box.write(path.join(dir, out), result.stdout);
    }
    return dir;
  }

  useCase('merge-clang-extdef-mappings.md', 'Example', async () => {
    const dir = await mappings();
    const output = path.join(root, 'externalDefMap.txt');
    expect(await tool('merge-clang-extdef-mappings', ['-i', dir, '-o', output], { cwd: root })).toExitWith(0);
    expect(lines(readFileSync(output, 'utf8')).sort()).toEqual(
      [
        `9:c:@F@main ${root}/main.c`,
        `11:c:@F@divide ${root}/main.c`,
        `14:c:@F@local_div ${root}/main.c`,
        `10:c:@F@deref ${root}/main.c`,
        `13:c:@F@get_zero ${root}/lib/util.c`,
        `13:c:@F@get_null ${root}/lib/util.c`,
      ].sort(),
    );
  }, NEEDS);

  useCase('merge-clang-extdef-mappings.md', 'Conflicting definitions are dropped', async () => {
    const dir = await mappings();
    const copy = readFileSync(path.join(dir, 'util.txt'), 'utf8').replaceAll(`${root}/lib/util.c`, `${root}/lib/util_copy.c`);
    box.write(path.join(dir, 'util_copy.txt'), copy);
    const output = path.join(root, 'conflict.txt');
    expect(await tool('merge-clang-extdef-mappings', ['-i', dir, '-o', output], { cwd: root })).toExitWith(0);
    const merged = lines(readFileSync(output, 'utf8'));
    expect(merged).toHaveLength(4);
    expect(merged.every((l) => l.endsWith(`${root}/main.c`))).toBe(true);
  }, NEEDS);
});

describe('usage/memory-safety-reporter.md', () => {
  useCase('memory-safety-reporter.md', 'Example', async () => {
    const demo = await sharedAnalysis('demo');
    const dir = box.newDir('msr');
    const prefix = path.join(dir, 'msr');
    const result = await tool(
      'memory_safety_reporter',
      ['-r', demo.reports, '-o', prefix, '-p', 'CXC123456', '-v', 'R1A01', '-b', 'app', '-d', 'build-42', '-t', '2026-10-23T23:00:00', '-x', 'zip'],
      { cwd: dir },
    );
    expect(result).toExitWith(0);
    const name = `${prefix}_CXC123456_R1A01_app_build-42_2026-10-23T23:00:00`;
    expect(result.stdout).toContain(`Creating package with name:  ${name}`);
    expect(statSync(name).isDirectory()).toBe(true);
    expect(readdirSync(name).sort()).toEqual(['CHECKSUMS.sha256', 'checker_details.json', 'config', 'metadata.json', 'reports.sarif']);
    expect(existsSync(`${name}.zip`)).toBe(true);
    expect(JSON.parse(readFileSync(path.join(name, 'metadata.json'), 'utf8'))).toEqual(
      JSON.parse(readFileSync(path.join(demo.reports, 'metadata.json'), 'utf8')),
    );
    const checksums = lines(readFileSync(path.join(name, 'CHECKSUMS.sha256'), 'utf8'));
    expect(checksums.map((l) => l.split('\t')[1]).sort()).toEqual(['checker_details.json', 'metadata.json', 'reports.sarif']);
    expect(checksums.every((l) => /^[0-9a-f]{64}\t/.test(l))).toBe(true);
    // The product information is only in the name.
    expect(readFileSync(path.join(name, 'reports.sarif'), 'utf8')).not.toContain('CXC123456');
  }, { requires: ['tool:memory_safety_reporter'] });

  useCase('memory-safety-reporter.md', 'Example', async () => {
    const missing = path.join(box.dir, 'no-such-reports');
    const result = await tool('memory_safety_reporter', ['-r', missing, '-o', path.join(box.dir, 'x')], { cwd: box.dir });
    expect(result).toExitWith(1);
    expect(result.output).toContain(`Report directory '${missing}' does not exist!`);
  }, { requires: ['tool:memory_safety_reporter'], name: 'missing report folder' });
});

describe('usage/bazel-compile-commands.md', () => {
  useCase('bazel-compile-commands.md', 'Example', async () => {
    if (isAvailable('command:bazel')) {
      // Without a Bazel workspace, only the command line handling is checked.
      expect(await tool('bazel-compile-commands', ['-b', '//...'], { cwd: box.dir })).toExitWith(1);
      return;
    }
    const noBazel = await tool('bazel-compile-commands', ['-b', 'bazel build //...', '-o', 'compile_commands.json'], { cwd: box.dir });
    expect(noBazel).toExitWith(1);
    expect(noBazel.output).toContain("No such file or directory: 'bazel'");
    // -b takes the whole command line; its first word is run.
    const wrong = await tool('bazel-compile-commands', ['-b', '//...'], { cwd: box.dir });
    expect(wrong).toExitWith(1);
    expect(wrong.output).toContain("'//...'");
  }, { requires: ['tool:bazel-compile-commands'] });
});

describe('usage/statistical-analysis.md', () => {
  useCase('statistical-analysis.md', 'Status on the test machine', async () => {
    const variants = [
      ['--stats'],
      ['--stats-collect', './stats'],
      ['--stats-use', './stats'],
      ['--stats', '--stats-min-sample-count', '5', '--stats-relevance-threshold', '0.9'],
    ];
    for (const [i, args] of variants.entries()) {
      const result = await cc(['analyze', 'compile_commands.json', '-o', `stats-${i}`, ...args], { cwd: root });
      if (isAvailable('clang-statistics-checkers')) {
        expect(result).toExitWith(0);
        continue;
      }
      // Without the statistics checkers, every form fails the same way.
      expect(result).toExitWith(1);
      expect(logMessages(result.output, 'ERROR')).toContain(
        'Statistics options can only be enabled if clang has statistics checkers available!',
      );
      expect(logMessages(result.output, 'INFO')).toEqual(
        expect.arrayContaining([
          'CodeChecker has not found Clang Static Analyzer checkers statisticsCollector.ReturnValueCheck and statisticsCollector.SpecialReturnValue',
          'hint: Maybe CodeChecker found the wrong analyzer binary?',
        ]),
      );
    }
  });

  useCase('statistical-analysis.md', 'post-process-stats tool', async () => {
    const input = box.newDir('pre_processed_stats');
    const output = path.join(box.dir, 'stats');
    const result = await tool('post-process-stats', ['-i', input, output], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toEqual(['Starting to post-process statistical results...', 'Done.']);
    expect(logMessages(result.output, 'WARNING')).toEqual(['No output files were found to collect statistics.']);
    expect(statSync(output).isDirectory()).toBe(true);
  }, { requires: ['tool:post-process-stats'] });
});
