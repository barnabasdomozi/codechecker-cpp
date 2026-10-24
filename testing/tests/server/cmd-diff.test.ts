// docs/usage/cmd-diff.md, the server parts of fixit.md and
// gerrit-integration.md.

import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { analyzeProject, sharedAnalysis, type Analysis } from '../../src/projects';
import { plainLocations, plainReports } from '../../src/reports';
import { store, useServer } from '../../src/server';
import { readFileSync } from 'node:fs';

const box = useSandbox();
const server = useServer(box);
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
  await store(server(), [demo.reports], 'demo', ['--tag', 'v1']);
  await store(server(), [demo2.reports], 'demo', ['--tag', 'v2']);
  await store(server(), [demo.reports, demo2.reports], 'multi');
});

const diff = (args: string[], env?: Record<string, string | undefined>) =>
  server().cc(['cmd', 'diff', ...args, '--url', server().url()], { env });

describe('usage/cmd-diff.md', () => {
  useCase('cmd-diff.md', 'Compare two tags of a run', async () => {
    const result = await diff(['-b', 'demo:v1', '-n', 'demo:v2', '--new']);
    expect(result).toExitWith(2);
    expect(plainLocations(plainReports(result.stdout))).toEqual(['main.c:21:16', 'main.c:21:18', 'style.cpp:4:35', 'style.cpp:4:35']);
    const resolved = await diff(['-b', 'demo:v1', '-n', 'demo:v2', '--resolved']);
    expect(plainReports(resolved.stdout).map((r) => r.checker)).toEqual(['clang-diagnostic-unused-variable']);
  });

  useCase('cmd-diff.md', 'Compare a server run with a local folder', async () => {
    const result = await diff(['-b', 'demo', '-n', demo2.reports, '--unresolved']);
    expect(result).toExitWith(2);
    // Cppcheck's style.cpp:11 report is left out: the client drops local
    // reports whose hash is suppressed in the source anywhere, and the
    // false positive on line 17 has the same hash (usage/cmd-diff.md).
    expect(plainLocations(plainReports(result.stdout))).toEqual([
      'main.c:11:12', 'main.c:11:12', 'main.c:21:16', 'main.c:21:18', 'main.c:25:10', 'main.c:25:10', 'style.cpp:4:35', 'style.cpp:4:35',
    ]);

    const nothingNew = await diff(['-b', 'demo', '-n', demo2.reports, '--new']);
    expect(nothingNew).toExitWith(0);
    expect(plainReports(nothingNew.stdout)).toEqual([]);
  });

  useCase('cmd-diff.md', 'Compare two different runs', async () => {
    const result = await diff(['-b', 'multi', '-n', 'demo', '--resolved', '--report-status', 'outstanding']);
    expect(result).toExitWith(2);
    expect(plainReports(result.stdout).length).toBeGreaterThan(0);
    const filtered = await diff(['-b', 'multi', '-n', 'demo', '--resolved', '--checker-name', 'no-such-checker']);
    expect(filtered).toExitWith(0);
  });

  useCase('cmd-diff.md', 'Fix only the new reports', async () => {
    await store(server(), [demo.reports], 'fixbase');
    const fx2 = await analyzeProject(box, 'demo2', ['--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr']);
    const result = await diff(['-b', 'fixbase', '-n', fx2.reports, '-o', 'json', '--new']);
    expect(result).toExitWith(2);
    const fix = await cc(['fixit', fx2.reports], { cwd: fx2.root, input: result.stdout });
    expect(fix).toExitWith(1);
    expect(fix.output).toContain("KeyError: 'checkedFile'");
  }, { knownIssue: 2, requires: ['command:clang-apply-replacements'] });

  useCase('fixit.md', 'Fix only selected reports (pipe a report list)', async () => {
    const own = await analyzeProject(box, 'demo', ['--analyzers', 'clang-tidy', '-e', 'modernize-use-nullptr', '-e', 'modernize-use-trailing-return-type']);
    await store(server(), [own.reports], 'fixrun');
    const selected = await server().cc(['cmd', 'results', 'fixrun', '-o', 'json', '--checker-name', 'modernize-use-nullptr', '--url', server().url()]);
    expect(selected).toExitWith(0);
    const fix = await cc(['fixit', own.reports], { cwd: own.root, input: selected.stdout });
    expect(fix).toExitWith(0);
    expect([...fix.stdout.matchAll(/DiagnosticName: (\S+)/g)].map((m) => m[1])).toEqual(['modernize-use-nullptr']);
  }, { name: 'cmd results -o json works', requires: ['command:clang-apply-replacements'] });

  useCase('gerrit-integration.md', 'Gerrit review JSON for new reports only', async () => {
    const changed = box.write('changed.json', JSON.stringify({ 'main.c': { lines_inserted: 1 }, 'style.cpp': { lines_inserted: 1 } }));
    const out = box.newDir('gerrit-server');
    const result = await diff(['-b', 'demo:v1', '-n', demo2.reports, '--new', '-o', 'gerrit', '-e', out], {
      CC_CHANGED_FILES: changed,
      CC_REPO_DIR: demo2.root,
      CC_REPORT_URL: 'http://ci/run/1',
    });
    expect(result).toExitWith(2);
    const review = JSON.parse(readFileSync(path.join(out, 'gerrit_review.json'), 'utf8')) as { message: string; comments: Record<string, unknown[]> };
    expect(review.message).toBe('CodeChecker found 4 issue(s) in the code. See: http://ci/run/1');
    expect(Object.keys(review.comments).sort()).toEqual(['main.c', 'style.cpp']);
  }, { name: 'the base is a server run' });
});
