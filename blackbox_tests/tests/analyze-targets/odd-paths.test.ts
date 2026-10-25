// blackbox_tests/analyze-targets/odd-paths

import { existsSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, reportRows } from '../../src/reports';

const box = useSandbox();

describe('project odd-paths', () => {
  test('the README table', async () => {
    const p = await sharedAnalysis('odd-paths');
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(projectReadme('odd-paths'), 'Expected results'));
  });

  test('exports and skipfiles handle the paths', async () => {
    const p = await sharedAnalysis('odd-paths');
    const html = path.join(box.newDir('html'), 'html');
    expect(await cc(['parse', p.reports, '-e', 'html', '-o', html], { cwd: p.root })).toExitWith(2);
    expect(existsSync(path.join(html, 'index.html'))).toBe(true);
    const skip = box.write('skip.txt', '-*/ünïcødé/*\n');
    const skipped = await reportRows(p.reports, p.root, ['-i', skip]);
    // Cppcheck's naïve.cpp report has a mangled path (known issue #21), so
    // the skipfile does not hide it.
    expect(skipped.map((r) => r.file).sort()).toEqual(
      ["src dir/main file.cpp", "src dir/main file.cpp", "src dir/quote's.cpp", 'src dir/xxnxxcxxdxx/naxxve.cpp'].sort(),
    );
  });
});
