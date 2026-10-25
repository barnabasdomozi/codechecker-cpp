// docs/usage/version.md

import { describe, expect } from '@jest/globals';
import { cc } from '../src/run';
import { useSandbox } from '../src/sandbox';
import { useCase } from '../src/usecase';
import { escapeRegExp, lines, logMessages } from '../src/normalize';

const box = useSandbox();

const ANALYZER_ROWS = ['Base package version', 'Package build date', 'Git commit ID (hash)', 'Git tag information'];
const WEB_ROWS = [...ANALYZER_ROWS, 'Server supported Thrift API version', 'Client Thrift API version'];

describe('usage/version.md', () => {
  useCase('version.md', 'Print all versions', async () => {
    const result = await cc(['version'], { cwd: box.dir });
    expect(result).toExitWith(0);
    const info = logMessages(result.output, 'INFO');
    expect(info).toContain('CodeChecker analyzer version:');
    expect(info).toContain('CodeChecker web version:');
    for (const row of WEB_ROWS) expect(result.stdout).toMatch(new RegExp(`│ ${escapeRegExp(row)} +│ \\S`));
    expect(result.stdout).toContain('│ Kind ');
  });

  useCase('version.md', 'Machine-readable version', async () => {
    const result = await cc(['version', '-o', 'json'], { cwd: box.dir });
    expect(result).toExitWith(0);
    const json = JSON.parse(result.stdout) as {
      analyzer: Record<string, unknown>;
      web: Record<string, unknown>;
    };
    expect(Object.keys(json.analyzer).sort()).toEqual(
      ['base_package_version', 'git_commit', 'git_tag', 'package_build_date'].sort(),
    );
    expect(Object.keys(json.web).sort()).toEqual(
      [
        'base_package_version',
        'client_api_version',
        'git_commit',
        'git_tag',
        'package_build_date',
        'server_api_version',
      ].sort(),
    );
    expect(json.analyzer.base_package_version).toMatch(/^\d+\.\d+\.\d+/);
    expect(json.web.base_package_version).toBe(json.analyzer.base_package_version);
    expect(json.analyzer.git_commit).toMatch(/^[0-9a-f]{40}$/);
    // The Thrift API version decides which clients and web UI can talk to
    // the server, so it must be the same as the reference.
    expect(json.web.client_api_version).toBe('6.74');
    expect(json.web.server_api_version).toEqual(['6.74']);
  });

  useCase('version.md', 'Only the analyzer or web package', async () => {
    const rows = await cc(['analyzer-version', '-o', 'rows'], { cwd: box.dir });
    expect(rows).toExitWith(0);
    const rowLines = lines(rows.stdout).filter((l) => l.trim() !== '');
    expect(rowLines.map((l) => ANALYZER_ROWS.find((r) => l.trim().startsWith(r)))).toEqual(ANALYZER_ROWS);
    expect(rows.stdout).not.toContain('Thrift');

    const csv = await cc(['web-version', '-o', 'csv'], { cwd: box.dir });
    expect(csv).toExitWith(0);
    const csvLines = lines(csv.stdout);
    expect(csvLines[0]).toBe('Kind,Version');
    expect(csvLines.slice(1).map((l) => l.split(',')[0])).toEqual(WEB_ROWS);
    expect(csv.stdout).toContain('Client Thrift API version,6.74');
  });

  useCase('version.md', 'Only the analyzer or web package', async () => {
    for (const command of ['version', 'analyzer-version', 'web-version']) {
      for (const format of ['rows', 'table', 'csv', 'json']) {
        const result = await cc([command, '-o', format], { cwd: box.dir });
        expect(result).toExitWith(0);
        if (format === 'json') expect(() => JSON.parse(result.stdout)).not.toThrow();
      }
    }
  }, { name: 'every output format exits 0' });
});
