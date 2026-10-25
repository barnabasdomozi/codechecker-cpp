// Removes values that change from run to run, so that output can be
// compared with the docs or with another build.

import { realpathSync } from 'node:fs';
import { sessionDir } from './config';

export interface NormalizeOptions {
  /** Paths to replace, e.g. { '<ROOT>': projectRoot }. Longest first. */
  paths?: Record<string, string>;
}

function realpathOrSelf(p: string): string {
  try {
    return realpathSync(p);
  } catch {
    return p;
  }
}

export function normalize(text: string, options: NormalizeOptions = {}): string {
  let out = text;
  const replacements: Array<[string, string]> = [];
  for (const [name, p] of Object.entries(options.paths ?? {})) {
    replacements.push([p, name]);
    const real = realpathOrSelf(p);
    if (real !== p) replacements.push([real, name]);
  }
  try {
    const session = sessionDir();
    replacements.push([session, '<SESSION>']);
    const real = realpathOrSelf(session);
    if (real !== session) replacements.push([real, '<SESSION>']);
  } catch {
    // Outside a test session.
  }
  replacements.sort((a, b) => b[0].length - a[0].length);
  for (const [from, to] of replacements) out = out.split(from).join(to);

  return (
    out
      // [INFO 2026-10-24 13:22:47] -> [INFO]
      .replace(/\[(INFO|WARNING|ERROR|DEBUG|DEBUG_ANALYZER|CRITICAL) \d{4}-\d\d-\d\d \d\d:\d\d:\d\d\]/g, '[$1]')
      .replace(/Analysis length: [\d.]+ sec\./g, 'Analysis length: <N> sec.')
      .replace(/\r\n/g, '\n')
  );
}

/** Lines of the text, without the trailing empty line. */
export function lines(text: string): string[] {
  const all = text.split('\n');
  if (all.length > 0 && all[all.length - 1] === '') all.pop();
  return all;
}

/** The rows of a box-drawn table (`│ a │ b │`), as arrays of trimmed cells. */
export function tableRows(text: string): string[][] {
  return lines(text)
    .filter((l) => l.startsWith('│'))
    .map((l) => l.split('│').slice(1, -1).map((c) => c.trim()));
}

/** Removes CodeChecker log lines ("[INFO] - ..."), keeping the other output. */
export function withoutLogLines(text: string): string {
  return lines(text)
    .filter((line) => !/^\[(INFO|WARNING|ERROR|DEBUG\w*|CRITICAL)( [\d: -]+)?\] - /.test(line))
    .join('\n');
}

/** The CodeChecker log lines of a level, without the prefix. */
export function logMessages(text: string, level: 'INFO' | 'WARNING' | 'ERROR' | 'DEBUG'): string[] {
  const re = new RegExp(`^\\[${level}(?: [\\d: -]+)?\\] - (.*)$`);
  const found: string[] = [];
  for (const line of lines(text)) {
    const match = re.exec(line);
    if (match) found.push(match[1]);
  }
  return found;
}

/** Escapes a string for use inside a regular expression. */
export function escapeRegExp(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
