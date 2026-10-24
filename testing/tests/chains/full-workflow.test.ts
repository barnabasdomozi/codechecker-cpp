// A full workflow with a server, as a CI pipeline would run it:
// log → analyze → parse → store → cmd runs/results/sum → cmd suppress →
// analyze a new version → store again → cmd diff → cmd export → cmd import.

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { chain } from '../../src/chain';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { lines } from '../../src/normalize';
import { DOC_ANALYZERS, copyProject } from '../../src/projects';
import { plainLocations, plainReports } from '../../src/reports';
import { ALL_RESULTS_FILTER, DETECTION_STATUS, REVIEW_STATUS, results, runs, useServer } from '../../src/server';

const box = useSandbox();
const server = useServer(box);

describe('chain: full workflow with a server', () => {
  const flow = chain('demo workflow');
  let parent = '';
  let root = '';
  let root2 = '';
  let exportFile = '';

  flow.step('log the build', async () => {
    parent = box.newDir('workflow');
    root = await copyProject('demo', parent);
    expect(await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root })).toExitWith(0);
  });

  flow.step('analyze', async () => {
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, '-n', 'main_branch'], { cwd: root })).toExitWith(0);
  });

  flow.step('parse', async () => {
    const result = await cc(['parse', 'reports'], { cwd: root });
    expect(result).toExitWith(2);
    expect(plainReports(result.stdout)).toHaveLength(6);
  });

  flow.step('store (run name from analyze -n)', async () => {
    expect(await server().cc(['store', path.join(root, 'reports'), '--tag', 'v1', '--url', server().url()])).toExitWith(0);
    expect(Object.keys(await runs(server()))).toEqual(['main_branch']);
  });

  flow.step('cmd runs, results, sum', async () => {
    const list = await runs(server());
    expect(list.main_branch.resultCount).toBe(6);
    const reports = await results(server(), ['main_branch']);
    expect(reports.map((r) => r.bugHash).sort()).toEqual(
      [
        '04d39a256ef58c412c5acf3f9280fe2e',
        '09296549497056a329a9ce22dda7d710',
        '249a121710b6a5cb467697f323de0d5f',
        '2e5ec744a6a360431ece6fe858d3f172',
        '3b2c4905253f6d0f7409d271d65dbdd6',
        '990560afd081e416b9c7cc4c772c4cff',
      ],
    );
    const sum = await server().cc(['cmd', 'sum', '-n', 'main_branch', '-o', 'json', '--url', server().url()]);
    expect(sum).toExitWith(0);
    expect((JSON.parse(sum.stdout) as Array<{ checker: string }>).map((r) => r.checker).sort()).toEqual(
      ['clang-diagnostic-uninitialized', 'clang-diagnostic-unused-variable', 'core.DivideZero', 'cppcheck-uninitvar', 'cppcheck-zerodiv'],
    );
  });

  flow.step('cmd suppress', async () => {
    const file = box.write('suppress.txt', '2e5ec744a6a360431ece6fe858d3f172||main.c||known, fixed later||intentional\n');
    expect(await server().cc(['cmd', 'suppress', 'main_branch', '-i', file, '--url', server().url()])).toExitWith(0);
    const reports = await results(server(), ['main_branch']);
    expect(reports.map((r) => r.bugHash.slice(0, 8)).sort()).toEqual(['04d39a25', '09296549', '249a1217', '3b2c4905', '990560af']);
  });

  flow.step('analyze the next version', async () => {
    root2 = await copyProject('demo2', parent);
    expect(await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root2 })).toExitWith(0);
    expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root2 })).toExitWith(0);
  });

  flow.step('cmd diff the change against the stored run', async () => {
    const result = await server().cc(['cmd', 'diff', '-b', 'main_branch', '-n', path.join(root2, 'reports'), '--new', '--url', server().url()]);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:21:16', 'main.c:21:18', 'style.cpp:4:35', 'style.cpp:4:35']);
  });

  flow.step('store the next version into the same run', async () => {
    expect(await server().cc(['store', path.join(root2, 'reports'), '-n', 'main_branch', '--tag', 'v2', '--url', server().url()])).toExitWith(0);
    const state = (await results(server(), ['main_branch'], ALL_RESULTS_FILTER)).map(
      (r) => `${r.bugHash.slice(0, 8)} ${DETECTION_STATUS[r.detectionStatus]} ${REVIEW_STATUS[r.reviewData.status]}`,
    );
    expect(state.sort()).toEqual([
      '04d39a25 unresolved unreviewed',
      '07a8f664 unresolved false_positive',
      '09296549 unresolved unreviewed',
      '0983180e new unreviewed',
      '19a15bf4 new unreviewed',
      '21b00907 unresolved false_positive',
      '249a1217 resolved unreviewed',
      // The review status set by `cmd suppress` is lost: storing the run
      // again replaces the report rows. (Reference behavior; the usage docs
      // do not mention it.)
      '2e5ec744 unresolved unreviewed',
      '3b2c4905 unresolved unreviewed',
      '990560af unresolved false_positive',
      '990560af unresolved unreviewed',
      'b8956890 new unreviewed',
      'dc01f098 new unreviewed',
    ]);
  });

  flow.step('cmd diff the two tags', async () => {
    const result = await server().cc(['cmd', 'diff', '-b', 'main_branch:v1', '-n', 'main_branch:v2', '--new', '--url', server().url()]);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:21:16', 'main.c:21:18', 'style.cpp:4:35', 'style.cpp:4:35']);
  });

  flow.step('cmd history', async () => {
    const result = await server().cc(['cmd', 'history', '-n', 'main_branch', '-o', 'csv', '--url', server().url()]);
    expect(result).toExitWith(0);
    expect(lines(result.stdout).filter((l) => l.includes(',main_branch,'))).toHaveLength(2);
  });

  flow.step('cmd suppress again', async () => {
    const file = box.write('suppress2.txt', '2e5ec744a6a360431ece6fe858d3f172||main.c||known, fixed later||intentional\n');
    expect(await server().cc(['cmd', 'suppress', 'main_branch', '-i', file, '--url', server().url()])).toExitWith(0);
    const reports = await results(server(), ['main_branch'], ALL_RESULTS_FILTER);
    expect(REVIEW_STATUS[reports.find((r) => r.bugHash.startsWith('2e5ec744'))!.reviewData.status]).toBe('intentional');
  });

  flow.step('cmd export', async () => {
    const result = await server().cc(['cmd', 'export', '-n', 'main_branch', '--url', server().url()]);
    expect(result).toExitWith(0);
    exportFile = box.write('export.json', result.stdout);
    const data = JSON.parse(readFileSync(exportFile, 'utf8')) as { reviewData: Record<string, { status: number }> };
    expect(data.reviewData['2e5ec744a6a360431ece6fe858d3f172'].status).toBe(3);
  });

  flow.step('cmd import into a new product', async () => {
    expect(await server().cc(['cmd', 'products', 'add', 'mirror', '--url', server().serverUrl])).toExitWith(0);
    expect(await server().cc(['store', path.join(root2, 'reports'), '-n', 'mirror_run', '--url', server().url('mirror')])).toExitWith(0);
    expect(await server().cc(['cmd', 'import', '-i', exportFile, '--url', server().url('mirror')])).toExitWith(0);
    const mirrored = await results(server(), ['mirror_run'], ALL_RESULTS_FILTER, 'mirror');
    expect(REVIEW_STATUS[mirrored.find((r) => r.bugHash.startsWith('2e5ec744'))!.reviewData.status]).toBe('intentional');
  });
});
