// docs/usage/store.md, the store parts of config-file.md and
// report-converter.md, and docs/reference/demo-project.md §3.

import { mkdirSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, run, tool } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages } from '../../src/normalize';
import { analyzeProject, builtExternalTools, sharedAnalysis, type Analysis } from '../../src/projects';
import { ALL_RESULTS_FILTER, results, runState, runs, store, useServer, privateReports } from '../../src/server';

const box = useSandbox();
const server = useServer(box);
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
});

/** `cmd runs -o csv` line of a run. */
async function csvLine(name: string): Promise<string> {
  const result = await server().cc(['cmd', 'runs', '-o', 'csv', '--url', server().url()]);
  expect(result).toExitWith(0);
  return lines(result.stdout).find((l) => l.startsWith(`${name},`)) ?? '';
}

/** docs/reference/demo-project.md §1 and §3. */
const DEMO_V1 = [
  '04d39a256ef58c412c5acf3f9280fe2e new unreviewed',
  '07a8f6647771d7458a6a8cf8e4b15643 new false_positive',
  '09296549497056a329a9ce22dda7d710 new unreviewed',
  '21b0090745a8a3abdf9a0989d93a8ce7 new false_positive',
  '249a121710b6a5cb467697f323de0d5f new unreviewed',
  '2e5ec744a6a360431ece6fe858d3f172 new unreviewed',
  '3b2c4905253f6d0f7409d271d65dbdd6 new unreviewed',
  '990560afd081e416b9c7cc4c772c4cff new false_positive',
  '990560afd081e416b9c7cc4c772c4cff new unreviewed',
];

describe('usage/store.md', () => {
  useCase('store.md', 'Store a run', async () => {
    const result = await store(server(), [demo.reports], 'demo', ['--tag', 'v1', '--description', 'first run']);
    expect(logMessages(result.output, 'INFO')).toContain('Storing the reports finished successfully.');
    expect(await runState(server(), 'demo')).toEqual(DEMO_V1);
    // The in-source review statuses are marked as such.
    const fps = (await results(server(), ['demo'], ALL_RESULTS_FILTER)).filter((r) => r.reviewData.status === 2);
    expect(fps.map((r) => r.reviewData.isInSource)).toEqual([true, true, true]);
    const line = await csvLine('demo');
    expect(line).toMatch(/^demo,6,clangsa \(3\/3\), clang-tidy \(3\/3\), cppcheck \(3\/3\),\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+,v1,0:00:00,first run,\S+/);
  });

  useCase('store.md', 'Store a run', async () => {
    const own = await analyzeProject(box, 'demo', ['-n', 'named_by_analyze']);
    const result = await server().cc(['store', privateReports(own.reports), '--url', server().url()]);
    expect(result).toExitWith(0);
    expect(Object.keys(await runs(server()))).toContain('named_by_analyze');
  }, { name: 'the run name from analyze -n' });

  useCase('store.md', 'Store an update of the same run', async () => {
    await store(server(), [demo.reports], 'update', ['--tag', 'v1']);
    await store(server(), [demo2.reports], 'update', ['--tag', 'v2']);
    expect(await runState(server(), 'update')).toEqual([
      '04d39a256ef58c412c5acf3f9280fe2e unresolved unreviewed',
      '07a8f6647771d7458a6a8cf8e4b15643 unresolved false_positive',
      '09296549497056a329a9ce22dda7d710 unresolved unreviewed',
      '0983180e376ce9254320b8353442adfa new unreviewed',
      '19a15bf46b7d3af4b189b3b4f2c28a5b new unreviewed',
      '21b0090745a8a3abdf9a0989d93a8ce7 unresolved false_positive',
      '249a121710b6a5cb467697f323de0d5f resolved unreviewed',
      '2e5ec744a6a360431ece6fe858d3f172 unresolved unreviewed',
      '3b2c4905253f6d0f7409d271d65dbdd6 unresolved unreviewed',
      '990560afd081e416b9c7cc4c772c4cff unresolved false_positive',
      '990560afd081e416b9c7cc4c772c4cff unresolved unreviewed',
      'b8956890be49041c8bdd2387242b08d8 new unreviewed',
      'dc01f09801754018c2543bd48099718f new unreviewed',
    ]);
    const resolved = (await results(server(), ['update'], ALL_RESULTS_FILTER)).find((r) => r.bugHash.startsWith('249a1217'))!;
    expect(resolved.fixedAt).not.toBeNull();
  });

  useCase('store.md', 'Store an update of the same run', async () => {
    await store(server(), [demo.reports], 'partial');
    const mainOnly = await analyzeProject(box, 'demo', ['--file', '*/main.c']);
    await store(server(), [mainOnly.reports], 'partial');
    const state = await runState(server(), 'partial');
    // Reports of files that were not analyzed this time are resolved too.
    expect(state.filter((s) => s.startsWith('21b00907') || s.startsWith('07a8f664'))).toEqual([
      '07a8f6647771d7458a6a8cf8e4b15643 resolved false_positive',
      '21b0090745a8a3abdf9a0989d93a8ce7 resolved false_positive',
    ]);
  }, { name: 'reports of files not stored again become resolved' });

  useCase('store.md', 'Replace the run completely (-f)', async () => {
    await store(server(), [demo.reports], 'replaced');
    await store(server(), [demo2.reports], 'replaced', ['-f']);
    const state = await runState(server(), 'replaced');
    expect(state).toHaveLength(12);
    expect(state.every((s) => s.includes(' new '))).toBe(true);
  });

  useCase('store.md', 'Shorten stored paths', async () => {
    await store(server(), [demo2.reports], 'trimmed', ['--trim-path-prefix', `${demo2.root}/`]);
    const files = new Set((await results(server(), ['trimmed'], ALL_RESULTS_FILTER)).map((r) => r.checkedFile));
    expect([...files].sort()).toEqual(['main.c', 'style.cpp']);
  });

  useCase('store.md', 'Store several folders as one run', async () => {
    await store(server(), [demo.reports, demo2.reports], 'multi');
    const reports = await results(server(), ['multi'], ALL_RESULTS_FILTER);
    const roots = new Set(reports.map((r) => path.dirname(r.checkedFile)));
    expect([...roots].sort()).toEqual([demo.root, demo2.root].sort());
    expect(reports).toHaveLength(21);
  });

  useCase('store.md', 'Read-only report folder (--zip-loc)', async () => {
    const zipDir = box.newDir('zipdir');
    const result = await server().cc(['store', privateReports(demo.reports), '-n', 'ziploc', '--zip-loc', zipDir, '--url', server().url()]);
    expect(result).toExitWith(0);
    expect(Object.keys(await runs(server()))).toContain('ziploc');
  });

  useCase('store.md', 'Read-only report folder (--zip-loc)', async () => {
    const missing = path.join(box.dir, 'no-such-zipdir');
    const result = await server().cc(['store', privateReports(demo.reports), '-n', 'ziploc2', '--zip-loc', missing, '--url', server().url()]);
    expect(result).toExitWith(1);
    expect(result.output).toContain('FileNotFoundError');
  }, { name: 'the folder must exist', knownIssue: 6 });

  useCase('store.md', 'Number of jobs', async () => {
    await store(server(), [demo.reports], 'jobs', ['-j', '1']);
    expect(await runState(server(), 'jobs')).toEqual(DEMO_V1);
  });

  useCase('store.md', 'Don\'t wait for the server (--detach)', async () => {
    const result = await server().cc(['store', privateReports(demo.reports), '-n', 'detached', '--detach', '--url', `http://${server().url()}`]);
    expect(result).toExitWith(0);
    const token = lines(result.stdout).filter((l) => l.trim() !== '').pop()!;
    expect(token).toMatch(/^[0-9a-f]{64}$/);
    expect(result.output).toContain(`CodeChecker cmd serverside-tasks --token ${token} --await`);
    const awaited = await server().cc(['cmd', 'serverside-tasks', '--token', token, '--await', '--url', server().serverUrl]);
    expect(awaited).toExitWith(0);
    expect(await runState(server(), 'detached')).toEqual(DEMO_V1);
  });

  useCase('store.md', 'Different products and HTTPS', async () => {
    expect(await server().cc(['cmd', 'products', 'add', 'proj2', '--url', server().serverUrl])).toExitWith(0);
    await store(server(), [demo.reports], 'p2run', [], 'proj2');
    expect(Object.keys(await runs(server(), [], 'proj2'))).toEqual(['p2run']);

    const missing = await server().cc(['store', privateReports(demo.reports), '-n', 'x', '--url', server().url('NoSuchProduct')]);
    expect(missing).toExitWith(1);
    expect(logMessages(missing.output, 'ERROR')).toContain(
      "Internal server error: The product with the given endpoint 'NoSuchProduct' does not exist!",
    );
  }, { name: 'products (HTTPS is in server.test.ts)' });

  useCase('store.md', 'Store results from other tools', async () => {
    const root = await builtExternalTools();
    const out = path.join(box.newDir('asan'), 'asan_results');
    expect(await tool('report-converter', ['-t', 'asan', '-o', out, path.join(root, 'out/heap_overflow.asan.out')], { cwd: root })).toExitWith(0);
    await store(server(), [out], 'asan_run');
    const reports = await results(server(), ['asan_run']);
    expect(reports.map((r) => `${r.checkerId} ${r.analyzerName}`)).toEqual(['AddressSanitizer.generic-error asan']);
  }, { requires: ['tool:report-converter', 'command:clang++'] });

  useCase('report-converter.md', 'Store converted results', async () => {
    const out = path.join(box.newDir('tidy'), 'rc');
    // Convert the clang-tidy plain output of the demo, then store it.
    const tidy = await run('clang-tidy', [path.join(demo.root, 'main.c'), "-checks=-*,clang-analyzer-*", '--'], { cwd: demo.root });
    const input = box.write('tidy.out', tidy.stdout);
    expect(await tool('report-converter', ['-t', 'clang-tidy', '-o', out, input], { cwd: box.dir })).toExitWith(0);
    await store(server(), [out], 'clang-tidy');
    const reports = await results(server(), ['clang-tidy']);
    expect(reports.map((r) => r.checkerId)).toEqual(['clang-analyzer-core.DivideZero']);
  }, { requires: ['tool:report-converter', 'command:clang-tidy'] });

  useCase('config-file.md', 'Store settings', async () => {
    const config = box.write('client.yaml', `store:\n  - --name=cfg_run\n  - --tag=from-config\n  - --url=${server().url()}\n`);
    expect(await cc(['store', privateReports(demo.reports), '--config', config], { cwd: box.dir })).toExitWith(0);
    expect(await csvLine('cfg_run')).toMatch(/,from-config,/);
  });

  useCase('store.md', 'Store a run', async () => {
    // Store defaults: ~/.codechecker/reports and localhost:8001/Default.
    mkdirSync(path.join(box.home, '.codechecker'), { recursive: true });
    const result = await cc(['store', '-n', 'nowhere', '--url', server().url()], { cwd: box.dir });
    expect(result).toExitWith(1);
  }, { name: 'without a report folder' });
});
