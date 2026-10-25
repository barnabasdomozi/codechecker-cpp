// docs/usage/log.md

import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { config } from '../src/config';
import { cc, sh } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { projectCopy } from '../src/projects';
import { findOnPath } from '../src/requirements';

const box = useSandbox();

interface Entry {
  directory: string;
  command: string;
  file: string;
}

function readDb(file: string): Entry[] {
  return JSON.parse(readFileSync(file, 'utf8')) as Entry[];
}

const gcc = () => findOnPath('gcc')!;
const gxx = () => findOnPath('g++')!;

/** The demo project's compile commands, as in usage/log.md. */
function demoEntries(root: string): Entry[] {
  return [
    { directory: root, command: `${gcc()} -c main.c -o main.o`, file: 'main.c' },
    { directory: root, command: `${gcc()} -c lib/util.c -o lib/util.o`, file: 'lib/util.c' },
    { directory: root, command: `${gxx()} -std=c++17 -c style.cpp -o style.o`, file: 'style.cpp' },
  ];
}

describe('usage/log.md', () => {
  useCase('log.md', 'Record a build', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toEqual(
      expect.arrayContaining(['Starting build...', 'Using CodeChecker ld-logger.', 'Build finished successfully.']),
    );
    // The build's own output is shown.
    expect(result.output).toContain('gcc -c main.c -o main.o');
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual(demoEntries(root));
  });

  useCase('log.md', 'Record a build', async () => {
    const root = await projectCopy(box, 'demo');
    writeFileSync(path.join(root, 'compile_commands.json'), '[{"directory": "/x", "command": "cc -c x.c", "file": "x.c"}]');
    expect(await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root })).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual(demoEntries(root));

    // Only files that are actually compiled are recorded.
    const again = await cc(['log', '-b', 'make', '-o', 'second.json'], { cwd: root });
    expect(again).toExitWith(0);
    expect(readDb(path.join(root, 'second.json'))).toEqual([]);
  }, { name: 'overwrites the output; nothing is recorded without a clean build' });

  useCase('log.md', 'Record a build', async () => {
    const root = await projectCopy(box, 'odd-paths');
    const result = await cc(['log', '-b', `cd '${root}' && ./build.sh`, '-o', path.join(root, 'cc.json')], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(readDb(path.join(root, 'cc.json')).map((e) => e.file).sort()).toEqual(
      ["src dir/main file.cpp", "src dir/quote's.cpp", 'src dir/ünïcødé/naïve.cpp'].sort(),
    );
  }, { name: 'any build command, e.g. a script' });

  useCase('log.md', 'Keep link commands (-k)', async () => {
    const root = await projectCopy(box, 'demo');
    expect(await cc(['log', '-b', 'make', '-o', 'compile_commands.json', '-k'], { cwd: root })).toExitWith(0);
    const entries = readDb(path.join(root, 'compile_commands.json'));
    expect(entries).toHaveLength(8);
    const link = `${gcc()} -o app main.o lib/util.o`;
    expect(entries).toContainEqual({ directory: root, command: link, file: 'main.o' });
    expect(entries).toContainEqual({ directory: root, command: link, file: 'lib/util.o' });

    const root2 = await projectCopy(box, 'demo');
    const env = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], {
      cwd: root2,
      env: { CC_LOGGER_KEEP_LINK: 'true' },
    });
    expect(env).toExitWith(0);
    expect(readDb(path.join(root2, 'compile_commands.json')).map((e) => [e.file, e.command])).toEqual(
      entries.map((e) => [e.file, e.command]),
    );
  });

  useCase('log.md', 'Hide the build output (-q)', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json', '-q'], { cwd: root });
    expect(result).toExitWith(0);
    expect(result.output).not.toContain('gcc -c main.c');
    expect(logMessages(result.output, 'INFO')).toContain('Build finished successfully.');
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual(demoEntries(root));
  });

  useCase('log.md', 'Load the logger by absolute path', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json', '--use-absolute-ldpreload-path'], {
      cwd: root,
    });
    expect(result).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual(demoEntries(root));
  });

  useCase('log.md', 'Load the logger by absolute path', async () => {
    // A build that resets LD_LIBRARY_PATH still gets the logger.
    const root = await projectCopy(box, 'demo');
    const result = await cc(
      ['log', '-b', 'env LD_LIBRARY_PATH= make', '-o', 'compile_commands.json', '--use-absolute-ldpreload-path'],
      { cwd: root },
    );
    expect(result).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual(demoEntries(root));
  }, { name: 'build resets LD_LIBRARY_PATH' });

  useCase('log.md', 'Debug the logger', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json', '--verbose', 'debug'], { cwd: root });
    expect(result).toExitWith(0);
    const debugFile = path.join(root, 'codechecker.logger.debug');
    expect(existsSync(debugFile)).toBe(true);
    expect(readFileSync(debugFile, 'utf8')).toContain('main.c');

    const root2 = await projectCopy(box, 'demo');
    const custom = path.join(box.newDir('debug'), 'logger.txt');
    const withEnv = await cc(['log', '-b', 'make', '-o', 'compile_commands.json', '--verbose', 'debug'], {
      cwd: root2,
      env: { CC_LOGGER_DEBUG_FILE: custom },
    });
    expect(withEnv).toExitWith(0);
    expect(existsSync(custom)).toBe(true);
    expect(existsSync(path.join(root2, 'codechecker.logger.debug'))).toBe(false);
  });

  useCase('log.md', 'Choose which compilers are logged (CC_LOGGER_GCC_LIKE)', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], {
      cwd: root,
      env: { CC_LOGGER_GCC_LIKE: 'g++' },
    });
    expect(result).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json')).map((e) => e.file)).toEqual(['style.cpp']);
  });

  useCase('log.md', 'Choose which compilers are logged (CC_LOGGER_GCC_LIKE)', async () => {
    // "/gcc" matches the binary name exactly, so g++ is not logged.
    const root = await projectCopy(box, 'demo');
    expect(
      await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root, env: { CC_LOGGER_GCC_LIKE: '/gcc' } }),
    ).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json')).map((e) => e.file)).toEqual(['main.c', 'lib/util.c']);
  }, { name: 'an entry starting with / matches the binary name exactly' });

  useCase('log.md', 'Add the compiler\'s implicit include paths (CC_LOGGER_DEF_DIRS)', async () => {
    const root = await projectCopy(box, 'demo');
    expect(
      await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root, env: { CC_LOGGER_DEF_DIRS: '1' } }),
    ).toExitWith(0);
    const main = readDb(path.join(root, 'compile_commands.json')).find((e) => e.file === 'main.c')!;
    expect(main.command.startsWith(`${gcc()} -I`)).toBe(true);
    expect(main.command).toContain('-I/usr/include ');
    expect(main.command.endsWith(' -c main.c -o main.o')).toBe(true);
  });

  useCase('log.md', 'Make include paths absolute (CC_LOGGER_ABS_PATH)', async () => {
    const root = await projectCopy(box, 'demo');
    const result = await cc(['log', '-b', 'gcc -Ilib -isystem lib -c main.c -o main.o', '-o', 'compile_commands.json'], {
      cwd: root,
      env: { CC_LOGGER_ABS_PATH: '1' },
    });
    expect(result).toExitWith(0);
    expect(readDb(path.join(root, 'compile_commands.json'))).toEqual([
      // The source path becomes absolute too.
      { directory: root, command: `${gcc()} -I${root}/lib -isystem ${root}/lib -c main.c -o main.o`, file: `${root}/main.c` },
    ]);
  });

  useCase('log.md', 'Use ldlogger directly, without CodeChecker', async () => {
    const installRoot = path.dirname(config().binDir);
    const library = path.join(installRoot, 'ld_logger', 'lib', '64bit', 'ldlogger.so');
    expect(existsSync(library)).toBe(true);
    const root = await projectCopy(box, 'demo');
    const output = path.join(root, 'compile_commands.json');
    const result = await sh('make', {
      cwd: root,
      env: { CC_LOGGER_GCC_LIKE: 'gcc:g++', CC_LOGGER_FILE: output, LD_PRELOAD: library },
    });
    expect(result).toExitWith(0);
    expect(readDb(output)).toEqual(demoEntries(root));
    // The package also has an `ldlogger` wrapper binary in bin/.
    expect(existsSync(path.join(config().binDir, 'ldlogger'))).toBe(true);
  });

  useCase('log.md', 'Exit code', async () => {
    const root = await projectCopy(box, 'build-failures');
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root });
    expect(result).toExitWith(2);
    // Failing compiler calls are logged too.
    expect(readDb(path.join(root, 'compile_commands.json')).map((e) => e.file)).toEqual([
      'good.cpp',
      'gcc_only_flag.cpp',
      'ignored_flag.cpp',
      'broken_syntax.cpp',
    ]);
    const custom = await cc(['log', '-b', 'exit 7', '-o', 'x.json'], { cwd: root });
    expect(custom).toExitWith(7);
  });
});
