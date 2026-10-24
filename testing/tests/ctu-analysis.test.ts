// docs/usage/ctu-analysis.md

import { existsSync, readdirSync, statSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { logMessages } from '../src/normalize';
import { DOC_ANALYZERS, logProject, projectCopy } from '../src/projects';
import { plainLocations, plainReports } from '../src/reports';

const box = useSandbox();
let root = '';

beforeAll(async () => {
  root = await projectCopy(box, 'demo');
  await logProject('demo', root);
});

const analyze = (args: string[]) => cc(['analyze', 'compile_commands.json', ...DOC_ANALYZERS, ...args], { cwd: root });
const divideZero = (text: string) =>
  plainLocations(plainReports(text).filter((r) => r.checker === 'core.DivideZero'));

function findFiles(dir: string, suffix: string): string[] {
  if (!existsSync(dir)) return [];
  const found: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) found.push(...findFiles(full, suffix));
    else if (entry.name.endsWith(suffix)) found.push(full);
  }
  return found;
}

describe('usage/ctu-analysis.md', () => {
  useCase('ctu-analysis.md', 'Run both phases', async () => {
    expect(await analyze(['-o', 'ctu', '--ctu', '--analyzers', 'clangsa'])).toExitWith(0);
    const parsed = await cc(['parse', 'ctu'], { cwd: root });
    expect(parsed).toExitWith(2);
    expect(divideZero(parsed.stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
    // The collected data is deleted afterwards.
    expect(existsSync(path.join(root, 'ctu', 'ctu-dir'))).toBe(false);

    expect(await analyze(['-o', 'no-ctu', '--analyzers', 'clangsa'])).toExitWith(0);
    expect(divideZero((await cc(['parse', 'no-ctu'], { cwd: root })).stdout)).toEqual(['main.c:11:12']);

    expect(await analyze(['-o', 'ctu-all', '--ctu-all', '--analyzers', 'clangsa'])).toExitWith(0);
    expect(divideZero((await cc(['parse', 'ctu-all'], { cwd: root })).stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
  });

  useCase('ctu-analysis.md', 'Run the phases separately', async () => {
    const collect = await analyze(['-o', 'phases', '--ctu-collect', '--analyzers', 'clangsa']);
    expect(collect).toExitWith(0);
    expect(logMessages(collect.output, 'INFO')).toContain('Pre-analysis finished.');
    const arch = path.join(root, 'phases', 'ctu-dir', 'x86_64');
    expect(existsSync(path.join(arch, 'externalDefMap.txt'))).toBe(true);
    expect(statSync(path.join(arch, 'ast')).isDirectory()).toBe(true);
    // No analysis ran.
    expect(readdirSync(path.join(root, 'phases')).filter((f) => f.endsWith('.plist'))).toEqual([]);

    const run = await analyze(['-o', 'phases', '--ctu-analyze', '--analyzers', 'clangsa']);
    expect(run).toExitWith(0);
    expect(existsSync(path.join(arch, 'externalDefMap.txt'))).toBe(true);
    expect(statSync(path.join(root, 'phases', 'ctu_connections')).isDirectory()).toBe(true);
    expect(readdirSync(path.join(root, 'phases', 'ctu_connections')).length).toBeGreaterThan(0);
    expect(divideZero((await cc(['parse', 'phases'], { cwd: root })).stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
  });

  useCase('ctu-analysis.md', 'AST mode', async () => {
    expect(await analyze(['-o', 'pch', '--ctu-collect', '--ctu-ast-mode', 'load-from-pch', '--analyzers', 'clangsa'])).toExitWith(0);
    const pch = path.join(root, 'pch', 'ctu-dir', 'x86_64');
    expect(findFiles(path.join(pch, 'ast'), '.ast').map((f) => path.relative(path.join(pch, 'ast'), f)).sort()).toEqual(
      [`${root}/lib/util.c.ast`, `${root}/main.c.ast`, `${root}/style.cpp.ast`].map((p) => p.slice(1)).sort(),
    );

    expect(
      await analyze(['-o', 'ondemand', '--ctu-collect', '--ctu-ast-mode', 'parse-on-demand', '--analyzers', 'clangsa']),
    ).toExitWith(0);
    const onDemand = path.join(root, 'ondemand', 'ctu-dir', 'x86_64');
    expect(existsSync(path.join(onDemand, 'invocation-list.yml'))).toBe(true);
    expect(findFiles(onDemand, '.ast')).toEqual([]);

    for (const mode of ['load-from-pch', 'parse-on-demand']) {
      expect(await analyze(['-o', `full-${mode}`, '--ctu', '--ctu-ast-mode', mode, '--analyzers', 'clangsa'])).toExitWith(0);
      expect(divideZero((await cc(['parse', `full-${mode}`], { cwd: root })).stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
    }
  });

  useCase('ctu-analysis.md', 'AST mode', async () => {
    expect(await analyze(['-o', 'default-mode', '--ctu-collect', '--analyzers', 'clangsa'])).toExitWith(0);
    const ast = path.join(root, 'default-mode', 'ctu-dir', 'x86_64', 'ast');
    // The help text says parse-on-demand is the default; AST files are written.
    expect(findFiles(ast, 'main.c.ast')).toHaveLength(1);
  }, { name: 'without --ctu-ast-mode, AST files are written' });

  useCase('ctu-analysis.md', 'Retry without CTU on failure', async () => {
    const result = await analyze(['-o', 'retry', '--ctu', '--ctu-reanalyze-on-failure', '--analyzers', 'clangsa']);
    expect(result).toExitWith(0);
    expect(divideZero((await cc(['parse', 'retry'], { cwd: root })).stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
  });

  useCase('ctu-analysis.md', 'With check', async () => {
    const result = await cc(['check', '-l', 'compile_commands.json', '--ctu', '--analyzers', 'clangsa'], { cwd: root });
    expect(result).toExitWith(2);
    expect(divideZero(result.stdout)).toEqual(['main.c:11:12', 'main.c:6:12']);
  });
});
