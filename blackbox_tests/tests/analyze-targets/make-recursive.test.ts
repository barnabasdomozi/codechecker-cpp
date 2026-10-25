// blackbox_tests/analyze-targets/make-recursive

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { logMessages } from '../../src/normalize';
import { projectReadme, sharedAnalysis } from '../../src/projects';
import { expectRowsToMatchTable, markdownTable, reportRows } from '../../src/reports';

const box = useSandbox();

describe('project make-recursive', () => {
  test('the README table', async () => {
    const p = await sharedAnalysis('make-recursive');
    expectRowsToMatchTable(await reportRows(p.reports, p.root), markdownTable(projectReadme('make-recursive'), 'Expected results'));
  });

  test('the compilation database', async () => {
    const p = await sharedAnalysis('make-recursive');
    const entries = JSON.parse(readFileSync(p.compileDb, 'utf8')) as Array<{ directory: string; file: string; command: string }>;
    expect(entries.map((e) => `${path.relative(p.root, e.directory)}/${e.file}`)).toEqual([
      'core/buffer.cpp',
      'core/codec.cpp',
      'core/codec.cpp',
      'tools/cli.cpp',
    ]);
    expect(entries[1].command).toContain('-DCODEC_FAST');
    expect(entries[1].command).toContain('-include ../config.h');
    expect(entries[3].command).toContain('@flags.rsp');
  });

  // The "Compile uniqueing" table of the README.
  const cases: Array<[string, number, string[]]> = [
    ['none', 0, ['core/codec.cpp:10:11', 'core/codec.cpp:14:27']],
    ['strict', 1, []],
    ['alpha', 0, ['core/codec.cpp:10:11']],
    ['.*-DCODEC_FAST.*', 0, ['core/codec.cpp:10:11']],
    ['(?!.*CODEC_FAST)', 0, ['core/codec.cpp:14:27']],
    // Matched from the start of the command, so it matches nothing, and the
    // first entry (codec_fast.o) is kept.
    ['-DCODEC_FAST', 0, ['core/codec.cpp:10:11']],
  ];
  for (const [mode, exit, codec] of cases) {
    test(`--compile-uniqueing '${mode}'`, async () => {
      const p = await sharedAnalysis('make-recursive');
      const out = path.join(box.newDir('uniq'), 'reports');
      const result = await cc(['analyze', 'compile_commands.json', '-o', out, `--compile-uniqueing=${mode}`, '--analyzers', 'clangsa'], { cwd: p.root });
      expect(result).toExitWith(exit);
      if (mode === 'strict') {
        expect(logMessages(result.output, 'ERROR').some((l) => l.startsWith('Build Action uniqueing failed as both '))).toBe(true);
        return;
      }
      const rows = await reportRows(out, p.root);
      expect(rows.filter((r) => r.file === 'core/codec.cpp').map((r) => r.location).sort()).toEqual(codec);
    });
  }
});
