// Short command chains without a server. Every step uses only what the
// earlier steps produced; nothing is prepared in advance.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect } from '@jest/globals';
import { chain } from '../../src/chain';
import { cc, run, tool } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { lines } from '../../src/normalize';
import { DOC_ANALYZERS, copyProject, projectReadme } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, plainLocations, plainReports, reportRows, type JsonReportFile } from '../../src/reports';
import { config } from '../../src/config';

const box = useSandbox();

describe('chain: log → analyze → parse', () => {
  const flow = chain('demo');
  let root = '';

  flow.step('copy the project', async () => {
    root = await copyProject('demo', box.newDir('chain1'));
  });

  flow.step('log', async () => {
    const result = await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root });
    expect(result).toExitWith(0);
    expect((JSON.parse(readFileSync(path.join(root, 'compile_commands.json'), 'utf8')) as unknown[]).length).toBe(3);
  });

  flow.step('analyze', async () => {
    const result = await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root });
    expect(result).toExitWith(0);
    expect(readdirSync(path.join(root, 'reports')).filter((f) => f.endsWith('.plist'))).toHaveLength(9);
  });

  flow.step('parse', async () => {
    const result = await cc(['parse', 'reports'], { cwd: root });
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:21:7', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });

  flow.step('parse -e json', async () => {
    const result = await cc(['parse', 'reports', '-e', 'json', '--review-status', 'unreviewed', 'confirmed', 'false_positive', 'intentional'], { cwd: root });
    expect(result).toExitWith(2);
    const json = JSON.parse(result.stdout) as JsonReportFile;
    expect(json.reports).toHaveLength(9);
    // The exact results of docs/reference/demo-project.md §1.
    const table = markdownTable(path.join(config().docsDir, 'reference', 'demo-project.md'), '1. `demo`');
    expectRowsToMatchTable(await reportRows(path.join(root, 'reports'), root), table);
  });

  flow.step('parse -e sarif', async () => {
    const out = path.join(root, 'results.sarif');
    expect(await cc(['parse', 'reports', '-e', 'sarif', '-o', out], { cwd: root })).toExitWith(2);
    const sarif = JSON.parse(readFileSync(out, 'utf8')) as { runs: Array<{ results: unknown[] }> };
    expect(sarif.runs.flatMap((r) => r.results)).toHaveLength(9);
  });

  flow.step('parse -e html', async () => {
    expect(await cc(['parse', 'reports', '-e', 'html', '-o', 'html'], { cwd: root })).toExitWith(2);
    expect(existsSync(path.join(root, 'html', 'index.html'))).toBe(true);
  });

  flow.step('parse -e baseline, then cmd diff against it', async () => {
    expect(await cc(['parse', 'reports', '-e', 'baseline', '-o', 'reports.baseline'], { cwd: root })).toExitWith(2);
    const diff = await cc(['cmd', 'diff', '-b', 'reports.baseline', '-n', 'reports', '--new'], { cwd: root });
    expect(diff).toExitWith(0);
  });
});

describe('chain: analyze two versions → cmd diff', () => {
  const flow = chain('demo and demo2');
  let parent = '';

  flow.step('copy, log and analyze both versions', async () => {
    parent = box.newDir('chain2');
    for (const name of ['demo', 'demo2'] as const) {
      const root = await copyProject(name, parent);
      expect(await cc(['log', '-b', 'make', '-o', 'compile_commands.json'], { cwd: root })).toExitWith(0);
      expect(await cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS], { cwd: root })).toExitWith(0);
    }
  });

  flow.step('cmd diff --new / --resolved / --unresolved', async () => {
    const a = path.join(parent, 'demo', 'reports');
    const b = path.join(parent, 'demo2', 'reports');
    const added = await cc(['cmd', 'diff', '-b', a, '-n', b, '--new'], { cwd: parent });
    expect(added).toExitWith(2);
    expect(plainLocations(plainReports(added.stdout))).toEqual(['main.c:21:16', 'main.c:21:18', 'style.cpp:4:35', 'style.cpp:4:35']);
    const gone = await cc(['cmd', 'diff', '-b', a, '-n', b, '--resolved'], { cwd: parent });
    expect(plainLocations(plainReports(gone.stdout))).toEqual(['main.c:21:7']);
    const kept = await cc(['cmd', 'diff', '-b', a, '-n', b, '--unresolved'], { cwd: parent });
    expect(plainLocations(plainReports(kept.stdout))).toEqual(['main.c:11:12', 'main.c:11:12', 'main.c:25:10', 'main.c:25:10', 'style.cpp:11:12']);
  });
});

describe('chain: sanitizer → report-converter → parse', () => {
  const flow = chain('external-tools');
  let root = '';

  flow.step('build and run the sanitized programs', async () => {
    root = await copyProject('external-tools', box.newDir('chain3'));
    expect(await run('make', ['asan', 'ubsan'], { cwd: root })).toExitWith(0);
  });

  flow.step('report-converter', async () => {
    expect(await tool('report-converter', ['-t', 'asan', '-o', 'rc', 'out/heap_overflow.asan.out', 'out/use_after_free.asan.out'], { cwd: root })).toExitWith(0);
    expect(await tool('report-converter', ['-t', 'ubsan', '-o', 'rc', 'out/undefined.ubsan.out'], { cwd: root })).toExitWith(0);
  });

  flow.step('parse the converted results', async () => {
    const table = markdownTable(projectReadme('external-tools'), 'Expected results').filter((r) =>
      ['-t asan', '-t ubsan'].some((c) => r.Conversion.startsWith(c)),
    );
    const rows = await reportRows(path.join(root, 'rc'), root);
    expect(rows.map((r) => `${r.location} ${r.checker} ${r.hash}`).sort()).toEqual(
      table.map((t) => `${t.Location} ${t.Checker} ${t['Report hash']}`).sort(),
    );
    const parsed = await cc(['parse', 'rc'], { cwd: root });
    expect(parsed).toExitWith(2);
    expect(lines(parsed.stdout).filter((l) => l.startsWith('[HIGH]'))).toHaveLength(4);
  });
});
