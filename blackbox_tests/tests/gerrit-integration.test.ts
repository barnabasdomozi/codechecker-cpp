// docs/usage/gerrit-integration.md (the server diff is in tests/server/)

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc, tool } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { sharedAnalysis, type Analysis } from '../src/projects';

const box = useSandbox();
let demo: Analysis;
let demo2: Analysis;
let changed = '';

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
  changed = box.write(
    'changed.json',
    JSON.stringify({
      '/COMMIT_MSG': { status: 'A', lines_inserted: 7, size_delta: 551, size: 551 },
      'main.c': { lines_inserted: 5, lines_deleted: 1, size_delta: 10, size: 700 },
    }),
  );
});

describe('usage/gerrit-integration.md', () => {
  useCase('gerrit-integration.md', 'Analyze only the changed files', async () => {
    const dir = box.newDir('skip');
    const named = await tool('gerrit_changed_files_to_skipfile.py', [changed, path.join(dir, 'skipfile.txt')], { cwd: dir });
    expect(named).toExitWith(0);
    expect(readFileSync(path.join(dir, 'skipfile.txt'), 'utf8')).toBe('+*/main.c\n-*\n');
    // The output defaults to ./skipfile.
    expect(await tool('gerrit_changed_files_to_skipfile.py', [changed], { cwd: dir })).toExitWith(0);
    expect(readFileSync(path.join(dir, 'skipfile'), 'utf8')).toBe('+*/main.c\n-*\n');
  }, { requires: ['tool:gerrit_changed_files_to_skipfile.py'] });

  useCase('gerrit-integration.md', 'Gerrit review JSON from local results', async () => {
    const out = path.join(box.newDir('gerrit'), 'gerrit_review.json');
    const env = { CC_REPO_DIR: demo.root, CC_CHANGED_FILES: changed, CC_REPORT_URL: 'http://ci.example.com/job/42' };
    expect(await cc(['parse', demo.reports, '-e', 'gerrit', '-o', out], { cwd: demo.root, env })).toExitWith(2);
    const review = JSON.parse(readFileSync(out, 'utf8')) as { message: string; labels: Record<string, number>; comments: Record<string, unknown[]> };
    const unchanged = "\n\nThere following reports are introduced in files which are not changed and can't be shown as individual reports:\n[HIGH] style.cpp:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n\n";
    expect(review.message).toBe(`CodeChecker found 6 issue(s) in the code.${unchanged} See: http://ci.example.com/job/42`);
    expect(review.comments['main.c']).toHaveLength(5);
    expect(review.labels).toEqual({ 'Code-Review': -1, Verified: -1 });

    const noUrl = await cc(['parse', demo.reports, '-e', 'gerrit'], { cwd: demo.root, env: { ...env, CC_REPORT_URL: undefined } });
    expect((JSON.parse(noUrl.stdout) as { message: string }).message).toBe(`CodeChecker found 6 issue(s) in the code.${unchanged}`);
  });

  useCase('gerrit-integration.md', 'Gerrit review JSON for new reports only', async () => {
    const out = box.newDir('diff_gerrit');
    const result = await cc(['cmd', 'diff', '-b', demo.reports, '-n', demo2.reports, '--new', '-o', 'gerrit', '-e', out], {
      cwd: box.dir,
      env: { CC_CHANGED_FILES: changed, CC_REPO_DIR: demo2.root },
    });
    expect(result).toExitWith(2);
    const review = JSON.parse(readFileSync(path.join(out, 'gerrit_review.json'), 'utf8')) as { message: string; comments: Record<string, Array<{ message: string }>> };
    // Only main.c changed, so only its new reports become comments.
    expect(Object.keys(review.comments)).toEqual(['main.c']);
    expect(review.comments['main.c'].map((c) => c.message.split('\n')[0]).sort()).toEqual([
      "[HIGH] main.c:21:18: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]",
      '[HIGH] main.c:21:16: Null pointer dereference: q [cppcheck-nullPointer]',
    ].sort());
    expect(review.message.startsWith('CodeChecker found 4 issue(s) in the code.')).toBe(true);
  });
});
