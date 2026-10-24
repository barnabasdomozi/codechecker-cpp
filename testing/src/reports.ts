// Reading reports: `parse -e json` output, plain-text report lines, and the
// expected-result tables in the Markdown docs.

import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect } from '@jest/globals';
import { cc, describeResult, type RunResult } from './run';
import { lines } from './normalize';

export const ALL_REVIEW_STATUSES = ['confirmed', 'false_positive', 'intentional', 'unreviewed'];

/** One report of `parse -e json` (only the fields the tests use). */
export interface JsonReport {
  analyzer_result_file_path: string;
  file: { id: string; path: string; original_path: string };
  line: number;
  column: number;
  message: string;
  checker_name: string;
  severity: string;
  report_hash: string;
  analyzer_name: string;
  review_status: string;
  bug_path_events: Array<{ file: { path: string }; line: number; column: number; message: string }>;
  [key: string]: unknown;
}

export interface JsonReportFile {
  version: number;
  reports: JsonReport[];
}

/** A report reduced to the columns of the expected-result tables. */
export interface ReportRow {
  /** `<file relative to the root>:<line>:<column>` */
  location: string;
  file: string;
  line: number;
  column: number;
  analyzer: string;
  checker: string;
  severity: string;
  hash: string;
  reviewStatus: string;
  message: string;
}

export function toRows(reports: JsonReport[], root: string): ReportRow[] {
  return reports
    .map((r) => {
      const file = path.relative(root, r.file.path);
      return {
        location: `${file}:${r.line}:${r.column}`,
        file,
        line: r.line,
        column: r.column,
        analyzer: r.analyzer_name,
        checker: r.checker_name,
        severity: r.severity,
        hash: r.report_hash,
        reviewStatus: r.review_status,
        message: r.message,
      };
    })
    .sort(compareRows);
}

function compareRows(a: ReportRow, b: ReportRow): number {
  return (
    a.file.localeCompare(b.file) ||
    a.line - b.line ||
    a.column - b.column ||
    a.checker.localeCompare(b.checker) ||
    a.analyzer.localeCompare(b.analyzer)
  );
}

/**
 * Runs `CodeChecker parse <inputs> -e json` and returns the parsed output.
 * By default every review status is included, so that nothing is hidden.
 */
export async function parseJson(
  inputs: string[],
  cwd: string,
  extraArgs: string[] = ['--review-status', ...ALL_REVIEW_STATUSES],
  env?: Record<string, string | undefined>,
): Promise<{ result: RunResult; json: JsonReportFile }> {
  const result = await cc(['parse', ...inputs, '-e', 'json', ...extraArgs], { cwd, env });
  if (result.exitCode !== 0 && result.exitCode !== 2) {
    throw new Error(`parse -e json failed:\n${describeResult(result)}`);
  }
  let json: JsonReportFile;
  try {
    json = JSON.parse(result.stdout) as JsonReportFile;
  } catch {
    throw new Error(`parse -e json did not print JSON:\n${describeResult(result)}`);
  }
  return { result, json };
}

/** Reports of a report folder as rows relative to `root`. */
export async function reportRows(reportDir: string, root: string, extraArgs?: string[]): Promise<ReportRow[]> {
  const { json } = await parseJson([reportDir], root, extraArgs);
  return toRows(json.reports, root);
}

// ---------------------------------------------------------------------------
// Plain-text report lines, as printed by parse, check and cmd diff.

export interface PlainReport {
  severity: string;
  path: string;
  line: number;
  column: number;
  message: string;
  checker: string;
  /** The review status suffix, e.g. "False positive", if printed. */
  reviewStatus?: string;
}

const PLAIN_RE = /^\[(CRITICAL|HIGH|MEDIUM|LOW|STYLE|UNSPECIFIED)\] (.+?):(\d+):(\d+): (.*) \[([^\]\s]+)\]$/;
const STATUS_SUFFIX_RE = / \[(False positive|Intentional|Confirmed|Suppress|Unreviewed)\]$/;

/** The report lines of a plain-text output. */
export function plainReports(text: string): PlainReport[] {
  const found: PlainReport[] = [];
  for (const line of lines(text)) {
    const status = STATUS_SUFFIX_RE.exec(line);
    const m = PLAIN_RE.exec(status ? line.slice(0, status.index) : line);
    if (!m) continue;
    found.push({
      severity: m[1],
      path: m[2],
      line: Number(m[3]),
      column: Number(m[4]),
      message: m[5],
      checker: m[6],
      reviewStatus: status?.[1],
    });
  }
  return found;
}

/** `{ checker: count }` of a list of reports (plain or JSON). */
export function checkerCounts(reports: Array<{ checker?: string; checker_name?: string }>): Record<string, number> {
  const counts: Record<string, number> = {};
  for (const r of reports) {
    const name = r.checker ?? r.checker_name ?? '?';
    counts[name] = (counts[name] ?? 0) + 1;
  }
  return counts;
}

/** `<base name>:<line>:<col>` of plain reports, sorted, for compact checks. */
export function plainLocations(reports: PlainReport[]): string[] {
  return reports.map((r) => `${path.basename(r.path)}:${r.line}:${r.column}`).sort();
}

// ---------------------------------------------------------------------------
// Expected-result tables in Markdown.

export type TableRow = Record<string, string>;

/**
 * Reads the first Markdown table after the heading whose text contains
 * `heading` (and, with `index`, the n-th table under it, before the next
 * heading). Cell text has backticks removed.
 */
export function markdownTable(file: string, heading: string, index = 0): TableRow[] {
  const all = lines(readFileSync(file, 'utf8'));
  const start = all.findIndex((l) => /^#+ /.test(l) && l.includes(heading));
  if (start < 0) throw new Error(`Heading '${heading}' not found in ${file}`);
  let tableCount = -1;
  for (let i = start + 1; i < all.length; i++) {
    if (/^#+ /.test(all[i])) break;
    if (all[i].startsWith('|') && i + 1 < all.length && /^\|[\s|:-]+\|$/.test(all[i + 1])) {
      tableCount += 1;
      if (tableCount !== index) continue;
      const header = splitRow(all[i]);
      const rows: TableRow[] = [];
      let lastFirstCell = '';
      for (let j = i + 2; j < all.length && all[j].startsWith('|'); j++) {
        const cells = splitRow(all[j]);
        const row: TableRow = {};
        header.forEach((h, k) => (row[h] = cells[k] ?? ''));
        // An empty first cell continues the group of the row above it.
        if (row[header[0]] === '' && lastFirstCell) row[header[0]] = lastFirstCell;
        lastFirstCell = row[header[0]];
        rows.push(row);
      }
      return rows;
    }
  }
  throw new Error(`Table ${index} after heading '${heading}' not found in ${file}`);
}

function splitRow(line: string): string[] {
  return line
    .replace(/^\|/, '')
    .replace(/\|$/, '')
    .split('|')
    .map((c) => c.trim().replace(/`/g, ''));
}

const COLUMN_TO_FIELD: Record<string, keyof ReportRow> = {
  Location: 'location',
  'File:line:col': 'location',
  Analyzer: 'analyzer',
  Checker: 'checker',
  Severity: 'severity',
  'Report hash': 'hash',
  'Review status': 'reviewStatus',
};

/** The table's value for a field: only the first word, e.g. "false_positive (in source)". */
function cellValue(column: string, value: string): string {
  if (column === 'Review status') return value.split(/\s+/)[0];
  return value;
}

/**
 * Checks that the reports are exactly the rows of an expected-result table,
 * comparing only the columns the table has.
 */
export function expectRowsToMatchTable(actual: ReportRow[], table: TableRow[]): void {
  const columns = Object.keys(table[0] ?? {}).filter((c) => c in COLUMN_TO_FIELD);
  const project = (row: ReportRow) => columns.map((c) => `${c}=${String(row[COLUMN_TO_FIELD[c]])}`).join(' | ');
  const expected = table.map((t) => columns.map((c) => `${c}=${cellValue(c, t[c])}`).join(' | ')).sort();
  expect(actual.map(project).sort()).toEqual(expected);
}
