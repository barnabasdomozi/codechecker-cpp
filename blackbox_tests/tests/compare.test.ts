// Comparison mode: with CODECHECKER_COMPARE_BIN set, both builds run on the
// same input and their results are compared. This finds differences that
// the docs do not cover. The suite does not need to know which build is
// which.

import { readdirSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { config } from '../src/config';
import { normalize } from '../src/normalize';
import { PROJECTS, copyProject, type ProjectName } from '../src/projects';
import { toRows, type JsonReportFile } from '../src/reports';
import { run, type RunResult } from '../src/run';
import { useSandbox } from '../src/sandbox';

const box = useSandbox();
const enabled = config().compareCodechecker !== undefined;

const PROJECT_NAMES: ProjectName[] = ['demo', 'demo2', 'cmake-app', 'make-recursive', 'ctu', 'review-status', 'build-failures', 'clean', 'odd-paths', 'analyzer-features'];

interface Build {
  label: string;
  binary: string;
}

const builds = (): Build[] => [
  { label: 'tested', binary: config().codechecker },
  { label: 'compared', binary: config().compareCodechecker! },
];

function ccWith(build: Build, args: string[], cwd: string): Promise<RunResult> {
  return run(build.binary, args, { cwd });
}

/** Logs, analyzes and parses a project with one build. */
async function analyzeWith(build: Build, name: ProjectName, extra: string[]) {
  const root = await copyProject(name, box.newDir(`${build.label}-${name}`));
  const log = await ccWith(build, ['log', '-b', PROJECTS[name].build, '-o', 'compile_commands.json'], root);
  const analyze = await ccWith(build, ['analyze', 'compile_commands.json', '-o', 'reports', ...extra], root);
  const json = await ccWith(build, ['parse', 'reports', '-e', 'json', '--review-status', 'unreviewed', 'confirmed', 'false_positive', 'intentional'], root);
  const plain = await ccWith(build, ['parse', 'reports'], root);
  const rows = json.stdout.trim().startsWith('{') ? toRows((JSON.parse(json.stdout) as JsonReportFile).reports, root) : [];
  return {
    exitCodes: [log.exitCode, analyze.exitCode, json.exitCode, plain.exitCode],
    rows: rows.map((r) => `${r.location} | ${r.analyzer} | ${r.checker} | ${r.severity} | ${r.hash} | ${r.reviewStatus} | ${r.message}`),
    // The lines of plain parse, with the project path replaced and the INFO
    // status block left out. Sorted: the order of the result-file groups is
    // not fixed, not even between two runs of the same build.
    plain: normalize(plain.stdout, { paths: { '<ROOT>': root } })
      .split('\n')
      .filter((l) => !l.startsWith('[INFO'))
      .sort(),
  };
}

describe('comparison of two builds', () => {
  if (!enabled) {
    test.skip('set CODECHECKER_COMPARE_BIN to compare two builds', () => undefined);
    return;
  }

  for (const name of PROJECT_NAMES) {
    for (const extra of [[], ['--ctu']]) {
      test(`${name}${extra.length ? ` ${extra.join(' ')}` : ''}: same exit codes, reports and parse output`, async () => {
        const [a, b] = await Promise.all(builds().map((build) => analyzeWith(build, name, extra)));
        expect(b.exitCodes).toEqual(a.exitCodes);
        expect(b.rows).toEqual(a.rows);
        expect(b.plain).toEqual(a.plain);
      });
    }
  }

  for (const args of [
    ['analyzers', '-o', 'json'],
    ['checkers', '-o', 'json'],
    ['checkers', '--profile', 'default', '-o', 'json'],
    ['checkers', '--details', '-o', 'json'],
    ['checkers', '--label'],
    ['analyzers', '--analyzer-config', 'clang-tidy', '-o', 'json'],
  ]) {
    test(`CodeChecker ${args.join(' ')}: same output`, async () => {
      const [a, b] = await Promise.all(builds().map((build) => ccWith(build, args, box.dir)));
      expect(b.exitCode).toBe(a.exitCode);
      // Listings without a fixed order are compared as sorted lines.
      const sorted = (r: RunResult) => r.stdout.split('\n').sort();
      expect(sorted(b)).toEqual(sorted(a));
    });
  }

  test('CodeChecker version -o json: same keys', async () => {
    const [a, b] = await Promise.all(builds().map((build) => ccWith(build, ['version', '-o', 'json'], box.dir)));
    const keys = (r: RunResult) => {
      const json = JSON.parse(r.stdout) as Record<string, Record<string, unknown>>;
      return Object.fromEntries(Object.entries(json).map(([k, v]) => [k, Object.keys(v).sort()]));
    };
    expect(keys(b)).toEqual(keys(a));
  });

  test('the report folder has the same files', async () => {
    const [a, b] = await Promise.all(
      builds().map(async (build) => {
        const root = await copyProject('demo', box.newDir(`${build.label}-files`));
        await ccWith(build, ['log', '-b', 'make', '-o', 'compile_commands.json'], root);
        await ccWith(build, ['analyze', 'compile_commands.json', '-o', 'reports'], root);
        // Result file names contain a hash of the compile command, which
        // includes the project path; leave it out.
        return readdirSync(path.join(root, 'reports'))
          .map((f) => f.replace(/_[0-9a-f]{32}\.plist$/, '_<hash>.plist'))
          .sort();
      }),
    );
    expect(b).toEqual(a);
  });
});
