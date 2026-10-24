// docs/usage/cmd-run-management.md, cmd-suppress.md, cmd-export-import.md,
// cmd-products.md, cmd-components.md, cmd-filter-preset.md and the
// "Saved filter presets" section of cmd-results.md.

import { existsSync } from 'node:fs';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { beforeAll, describe, expect } from '@jest/globals';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages } from '../../src/normalize';
import { sharedAnalysis, type Analysis } from '../../src/projects';
import { ALL_RESULTS_FILTER, REVIEW_STATUS, results, runs, store, useServer, privateReports } from '../../src/server';

const box = useSandbox();
const server = useServer(box);
let demo: Analysis;
let demo2: Analysis;

beforeAll(async () => {
  [demo, demo2] = await Promise.all([sharedAnalysis('demo'), sharedAnalysis('demo2')]);
});

const url = () => server().url();
const cmd = (args: string[], input?: string) => server().cc(['cmd', ...args], { input });

function tableRows(text: string): string[][] {
  return lines(text)
    .filter((l) => l.startsWith('│'))
    .map((l) => l.split('│').slice(1, -1).map((c) => c.trim()));
}

async function reviewStatusOf(run: string, hashPrefix: string, product = 'Default'): Promise<string> {
  const report = (await results(server(), [run], ALL_RESULTS_FILTER, product)).find((r) => r.bugHash.startsWith(hashPrefix));
  return report ? REVIEW_STATUS[report.reviewData.status] : 'missing';
}

describe('usage/cmd-run-management.md', () => {
  useCase('cmd-run-management.md', 'Rename a run', async () => {
    await store(server(), [demo.reports], 'to_rename');
    const result = await cmd(['update', 'to_rename', '-n', 'renamed', '--url', url()]);
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toContain('Done.');
    const names = Object.keys(await runs(server()));
    expect(names).toContain('renamed');
    expect(names).not.toContain('to_rename');
    expect(await cmd(['update', 'no_such_run', '-n', 'x', '--url', url()])).toExitWith(1);
  });

  useCase('cmd-run-management.md', 'Delete runs', async () => {
    for (const name of ['del_a', 'del_b', 'del_c', 'del_d', 'del_e']) {
      await store(server(), [demo.reports], name);
      await sleep(1100);
    }
    const single = await cmd(['del', '-n', 'del_a', '--url', url()]);
    expect(single).toExitWith(0);
    expect(logMessages(single.output, 'INFO')).toContain('Done.');
    expect(await cmd(['del', '-n', 'del_b', 'del_c', '--url', url()])).toExitWith(0);
    let names = Object.keys(await runs(server(), ['-n', 'del_*']));
    expect(names.sort()).toEqual(['del_d', 'del_e']);

    expect(await cmd(['del', '--all-after-run', 'del_d', '--url', url()])).toExitWith(0);
    names = Object.keys(await runs(server(), ['-n', 'del_*']));
    expect(names).toEqual(['del_d']);
    expect(await cmd(['del', '--all-before-time', '2000:01:01', '--url', url()])).toExitWith(0);
    expect(Object.keys(await runs(server(), ['-n', 'del_*']))).toEqual(['del_d']);
    // Nothing matched: still 0.
    expect(await cmd(['del', '-n', 'no_such_run', '--url', url()])).toExitWith(0);
    // Exactly one selector is required.
    expect(await cmd(['del', '--url', url()])).toExitWith(1);
  });

  useCase('cmd-run-management.md', 'Delete runs', async () => {
    await store(server(), [demo.reports], 'old_one');
    await sleep(1100);
    const middle = new Date();
    await sleep(1100);
    await store(server(), [demo.reports], 'new_one');
    const stamp = (d: Date) =>
      [d.getFullYear(), d.getMonth() + 1, d.getDate(), d.getHours(), d.getMinutes(), d.getSeconds()].map(String).join(':');
    expect(await cmd(['del', '--all-after-time', stamp(middle), '--url', url()])).toExitWith(0);
    const names = Object.keys(await runs(server()));
    expect(names).toContain('old_one');
    expect(names).not.toContain('new_one');
    expect(await cmd(['del', '--all-before-run', 'old_one', '--url', url()])).toExitWith(0);
  }, { name: 'by time and before a run' });
});

describe('usage/cmd-suppress.md', () => {
  useCase('cmd-suppress.md', 'Import', async () => {
    await store(server(), [demo2.reports], 'sup');
    const file = box.write('srv_suppress.txt', '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||reviewed, not a real issue||false_positive\n');
    const result = await cmd(['suppress', 'sup', '-i', file, '--url', url()]);
    expect(result).toExitWith(0);
    const fps = await results(server(), ['sup'], ['--review-status', 'false_positive', '--checker-name', 'core.NullDereference']);
    expect(fps.map((r) => `${r.bugHash} ${r.reviewData.comment}`)).toEqual(['19a15bf46b7d3af4b189b3b4f2c28a5b reviewed, not a real issue']);
    // Unmatched entries are ignored.
    const other = box.write('other.txt', '00000000000000000000000000000000||main.c||nothing||false_positive\n');
    expect(await cmd(['suppress', 'sup', '-i', other, '--url', url()])).toExitWith(0);
    expect(await cmd(['suppress', 'no_such_run', '-i', file, '--url', url()])).toExitWith(1);
  });

  useCase('cmd-suppress.md', 'Overwrite existing review statuses (-f)', async () => {
    await store(server(), [demo2.reports], 'sup_f');
    const first = box.write('first.txt', '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||first||false_positive\n');
    const second = box.write('second.txt', '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||second||confirmed\n');
    expect(await cmd(['suppress', 'sup_f', '-i', first, '--url', url()])).toExitWith(0);
    expect(await reviewStatusOf('sup_f', '19a15bf4')).toBe('false_positive');
    // Existing review statuses are overwritten with and without -f
    // (usage/cmd-suppress.md; the help text says otherwise).
    expect(await cmd(['suppress', 'sup_f', '-i', second, '--url', url()])).toExitWith(0);
    expect(await reviewStatusOf('sup_f', '19a15bf4')).toBe('confirmed');
    const third = box.write('third.txt', '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||third||intentional\n');
    expect(await cmd(['suppress', 'sup_f', '-i', third, '-f', '--url', url()])).toExitWith(0);
    expect(await reviewStatusOf('sup_f', '19a15bf4')).toBe('intentional');
  });
});

describe('usage/cmd-export-import.md', () => {
  useCase('cmd-export-import.md', 'Export', async () => {
    await store(server(), [demo.reports], 'exp');
    const result = await cmd(['export', '-n', 'exp', '--url', url()]);
    expect(result).toExitWith(0);
    // Only JSON on stdout; log lines go to stderr.
    const data = JSON.parse(result.stdout) as { comments: Record<string, unknown>; reviewData: Record<string, Record<string, unknown>> };
    // Keyed by hash: Cppcheck's two style.cpp reports share 990560af..., of
    // which only the one on line 17 is a false positive.
    expect(Object.keys(data.reviewData).sort()).toEqual(['07a8f6647771d7458a6a8cf8e4b15643', '21b0090745a8a3abdf9a0989d93a8ce7', '990560afd081e416b9c7cc4c772c4cff']);
    expect(data.reviewData['21b0090745a8a3abdf9a0989d93a8ce7']).toMatchObject({
      status: 2,
      comment: 'intentional for the demo',
      author: 'Anonymous',
      isInSource: true,
    });
    expect(data.comments).toEqual({});

    const all = await cmd(['export', '--url', url()]);
    expect(all).toExitWith(0);
    expect(() => JSON.parse(all.stdout)).not.toThrow();
  });

  useCase('cmd-export-import.md', 'Import', async () => {
    // Set a status on the server (not in the source), export it, and import
    // it into a second product with the same reports.
    await store(server(), [demo2.reports], 'imp_src');
    const file = box.write('imp_suppress.txt', '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||moved||intentional\n');
    expect(await cmd(['suppress', 'imp_src', '-i', file, '--url', url()])).toExitWith(0);
    const exported = await cmd(['export', '-n', 'imp_src', '--url', url()]);
    const exportFile = box.write('export.json', exported.stdout);

    expect(await cmd(['products', 'add', 'copy', '--url', server().serverUrl])).toExitWith(0);
    await store(server(), [demo2.reports], 'imp_dst', [], 'copy');
    expect(await reviewStatusOf('imp_dst', '19a15bf4', 'copy')).toBe('unreviewed');
    const imported = await cmd(['import', '-i', exportFile, '--url', server().url('copy')]);
    expect(imported).toExitWith(0);
    expect(logMessages(imported.output, 'INFO')).toContain('Import successful!');
    expect(await reviewStatusOf('imp_dst', '19a15bf4', 'copy')).toBe('intentional');

    expect(await cmd(['import', '-i', box.write('bad.json', 'not json'), '--url', url()])).toExitWith(1);
  });
});

describe('usage/cmd-products.md', () => {
  useCase('cmd-products.md', 'List products', async () => {
    const result = await cmd(['products', 'list', '--url', server().serverUrl]);
    expect(result).toExitWith(0);
    const rows = tableRows(result.stdout);
    expect(rows[0]).toEqual(['Database status', 'Endpoint', 'Name', 'Description']);
    expect(rows).toContainEqual(['Database is up to date.', 'Default', 'Default', 'Default product created at server start.']);
    const json = await cmd(['products', 'list', '-o', 'json', '--url', server().serverUrl]);
    expect(json).toExitWith(0);
    expect(JSON.stringify(JSON.parse(json.stdout))).toContain('Default');
  });

  useCase('cmd-products.md', 'Add a product (SQLite)', async () => {
    const added = await cmd(['products', 'add', 'proj2', '-n', 'Project Two', '--description', 'second product', '--report-limit', '3', '--url', server().serverUrl]);
    expect(added).toExitWith(0);
    expect(logMessages(added.output, 'INFO')).toContain('Product added successfully.');
    const rows = tableRows((await cmd(['products', 'list', '--url', server().serverUrl])).stdout);
    expect(rows).toContainEqual(['Database is up to date.', 'proj2', 'Project Two', 'second product']);
    expect(existsSync(path.join(server().workspace, 'proj2.sqlite'))).toBe(true);

    // A store with more reports than the limit is rejected.
    const tooMany = await server().cc(['store', privateReports(demo.reports), '-n', 'big', '--url', server().url('proj2')]);
    expect(tooMany).toExitWith(1);

    expect(await cmd(['products', 'add', 'proj3', '--sqlite', 'proj3.sqlite', '--url', server().serverUrl])).toExitWith(0);
    expect(existsSync(path.join(server().workspace, 'proj3.sqlite'))).toBe(true);
    await store(server(), [demo.reports], 'p3', [], 'proj3');
    expect(Object.keys(await runs(server(), [], 'proj3'))).toEqual(['p3']);
  });

  useCase('cmd-products.md', 'Remove a product', async () => {
    expect(await cmd(['products', 'add', 'gone', '--url', server().serverUrl])).toExitWith(0);
    const removed = await cmd(['products', 'del', 'gone', '--url', server().serverUrl]);
    expect(removed).toExitWith(0);
    expect(logMessages(removed.output, 'INFO')).toContain('Product removed.');
    expect(tableRows((await cmd(['products', 'list', '--url', server().serverUrl])).stdout).map((r) => r[1])).not.toContain('gone');
    // The database file stays.
    expect(existsSync(path.join(server().workspace, 'gone.sqlite'))).toBe(true);
    // Removing a missing product is not an error.
    expect(await cmd(['products', 'del', 'never_existed', '--url', server().serverUrl])).toExitWith(0);
  });
});

describe('usage/cmd-components.md', () => {
  useCase('cmd-components.md', 'Add or update a component', async () => {
    const file = box.write('comp.txt', '+*/lib/*\n-*/lib/skip/*\n');
    const added = await cmd(['components', 'add', 'libcomp', '-i', file, '--description', 'Library code', '--url', url()]);
    expect(added).toExitWith(0);
    expect(logMessages(added.output, 'INFO')).toContain('Source component added.');

    const update = box.write('comp3.txt', '+*/lib/util.c\n');
    const again = await cmd(['components', 'add', 'libcomp', '-i', update, '--url', url()], 'y\n');
    expect(again).toExitWith(0);
    expect(again.output).toContain("The source component 'libcomp' already exist!");
    expect(again.output).toContain('Do you want to update? Y(es)/n(o)');
    const list = JSON.parse((await cmd(['components', 'list', '-o', 'json', '--url', url()])).stdout) as Array<{ name: string; value: string | null }>;
    expect(list.find((c) => c.name === 'libcomp')?.value).toBe('+*/lib/util.c');
  });

  useCase('cmd-components.md', 'Add or update a component', async () => {
    const file = box.write('eof.txt', '+*/x/*\n');
    expect(await cmd(['components', 'add', 'eofcomp', '-i', file, '--url', url()])).toExitWith(0);
    const again = await cmd(['components', 'add', 'eofcomp', '-i', file, '--url', url()]);
    expect(again).toExitWith(1);
    expect(again.output).toContain('EOFError');
  }, { name: 'updating without input crashes', knownIssue: 7 });

  useCase('cmd-components.md', 'List components', async () => {
    const file = box.write('listcomp.txt', '+*/lib/*\n-*/lib/skip/*\n');
    expect(await cmd(['components', 'add', 'listcomp', '-i', file, '--description', 'Library code', '--url', url()])).toExitWith(0);
    const table = await cmd(['components', 'list', '--url', url()]);
    expect(table).toExitWith(0);
    const rows = tableRows(table.stdout);
    expect(rows[0]).toEqual(['Name', 'Value', 'Description']);
    const at = rows.findIndex((r) => r[0] === 'listcomp');
    expect(rows[at]).toEqual(['listcomp', '+*/lib/*', 'Library code']);
    expect(rows[at + 1]).toEqual(['', '-*/lib/skip/*', '']);
    expect(rows.map((r) => r[0])).toContain('Other (auto-generated)');

    const json = JSON.parse((await cmd(['components', 'list', '-o', 'json', '--url', url()])).stdout) as Array<{ name: string; value: string | null; description: string | null }>;
    expect(json).toContainEqual({
      name: 'Other (auto-generated)',
      value: null,
      description: 'Special auto-generated source component which contains files that are uncovered by the rest of the components.',
    });
  });

  useCase('cmd-components.md', 'Filter results by component', async () => {
    await store(server(), [demo.reports, demo2.reports], 'comp_multi');
    await store(server(), [demo2.reports], 'comp_trimmed', ['--trim-path-prefix', `${demo2.root}/`]);
    const comp = box.write('comp2.txt', '+*/main.c\n');
    expect(await cmd(['components', 'add', 'maincomp', '-i', comp, '--url', url()])).toExitWith(0);
    const multi = await results(server(), ['comp_multi'], ['--component', 'maincomp']);
    expect(multi.length).toBeGreaterThan(0);
    expect(multi.every((r) => r.checkedFile.endsWith('/main.c'))).toBe(true);
    // Stored paths like "main.c" have no "/" before the name.
    expect(await results(server(), ['comp_trimmed'], ['--component', 'maincomp'])).toEqual([]);

    const loose = box.write('comp4.txt', '+*main.c\n');
    expect(await cmd(['components', 'add', 'maincomp', '-i', loose, '--url', url()], 'y\n')).toExitWith(0);
    const trimmed = await results(server(), ['comp_trimmed'], ['--component', 'maincomp']);
    expect(trimmed.length).toBeGreaterThan(0);
    expect(trimmed.every((r) => r.checkedFile === 'main.c')).toBe(true);
  });

  useCase('cmd-components.md', 'Delete a component', async () => {
    const file = box.write('delcomp.txt', '+*/lib/*\n');
    expect(await cmd(['components', 'add', 'delcomp', '-i', file, '--url', url()])).toExitWith(0);
    const removed = await cmd(['components', 'del', 'delcomp', '--url', url()]);
    expect(removed).toExitWith(0);
    expect(logMessages(removed.output, 'INFO')).toContain('Source component removed.');
    const list = JSON.parse((await cmd(['components', 'list', '-o', 'json', '--url', url()])).stdout) as Array<{ name: string }>;
    expect(list.map((c) => c.name)).not.toContain('delcomp');
  });
});

describe('usage/cmd-filter-preset.md', () => {
  let presetRun = '';
  beforeAll(async () => {
    presetRun = 'preset_run';
    await store(server(), [demo.reports], presetRun);
    await store(server(), [demo2.reports], presetRun);
  });

  async function presets(): Promise<Array<Record<string, unknown>>> {
    const result = await cmd(['filter-preset', 'list', '-o', 'json', '--url', url()]);
    expect(result).toExitWith(0);
    return JSON.parse(result.stdout) as Array<Record<string, unknown>>;
  }

  const idOf = async (name: string) => {
    const preset = (await presets()).find((p) => Object.values(p).includes(name))!;
    return String(preset.id ?? preset.ID);
  };

  useCase('cmd-filter-preset.md', 'Create a preset', async () => {
    const result = await cmd(['filter-preset', 'new', '--name', 'high_core', '--severity', 'high', '--checker-name', 'core.*', '--url', url()]);
    expect(result).toExitWith(0);
    expect(result.output).toContain('Severity: HIGH');
    expect(result.output).toContain('Is unique: no');
  });

  useCase('cmd-filter-preset.md', 'List presets', async () => {
    const table = await cmd(['filter-preset', 'list', '--url', url()]);
    expect(table).toExitWith(0);
    const rows = tableRows(table.stdout);
    expect(rows[0]).toEqual(['ID', 'Name', 'Active Filters']);
    expect(rows.find((r) => r[1] === 'high_core')?.[2]).toBe('checkerName(1), severity(1), uniqueing');
    const csv = await cmd(['filter-preset', 'list', '-o', 'csv', '--url', url()]);
    expect(lines(csv.stdout)[0]).toBe('ID,Name,Active Filters');
    expect(lines(csv.stdout).some((l) => /^\d+,high_core,checkerName\(1\), severity\(1\), uniqueing$/.test(l))).toBe(true);
    expect((await presets()).length).toBeGreaterThan(0);
  });

  useCase('cmd-filter-preset.md', 'Use a preset', async () => {
    const reports = await results(server(), [presetRun], ['--filter-preset', 'high_core']);
    // The preset replaces all filters: false positives are listed too.
    expect(reports.map((r) => `${r.bugHash.slice(0, 8)} ${REVIEW_STATUS[r.reviewData.status]}`).sort()).toEqual(
      ['07a8f664 false_positive', '09296549 unreviewed', '19a15bf4 unreviewed', '21b00907 false_positive', 'b8956890 unreviewed'].sort(),
    );
  });

  useCase('cmd-results.md', 'Saved filter presets', async () => {
    const plain = await results(server(), [presetRun], ['--severity', 'high', '--checker-name', 'core.*']);
    const preset = await results(server(), [presetRun], ['--filter-preset', 'high_core']);
    // The preset's review-status filter replaces the default.
    expect(preset.length).toBe(plain.length + 2);
    const sum = await cmd(['sum', '-n', presetRun, '--filter-preset', 'high_core', '-o', 'json', '--url', url()]);
    expect(sum).toExitWith(0);
  });

  useCase('cmd-filter-preset.md', 'Rename a preset', async () => {
    const id = await idOf('high_core');
    const result = await cmd(['filter-preset', 'rename', '--preset-id', id, '--new-name', 'core_high', '--url', url()]);
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toContain(`Filter preset (ID: ${id}) renamed to 'core_high'.`);
  });

  useCase('cmd-filter-preset.md', 'Delete a preset', async () => {
    expect(await cmd(['filter-preset', 'new', '--name', 'tmp', '--severity', 'low', '--url', url()])).toExitWith(0);
    const id = await idOf('tmp');
    const result = await cmd(['filter-preset', 'delete', '--id', id, '--url', url()]);
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO')).toContain(`Filter preset 'tmp' (ID: ${id}) successfully deleted.`);
    expect((await presets()).some((p) => Object.values(p).includes('tmp'))).toBe(false);
  });
});
