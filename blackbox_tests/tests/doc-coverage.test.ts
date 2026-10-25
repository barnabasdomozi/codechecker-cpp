// Compares the use cases in docs/usage with the tests that cover them.
//
// - Every `useCase('<page>', '<heading>', ...)` must name an existing
//   heading of that page (catches typos and renamed headings).
// - Every documented use case (a `##`/`###` heading marked ✅, ⚠️ or ❌)
//   without a test is listed as a Jest "todo".

import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, test } from '@jest/globals';
import { config } from '../src/config';

interface Heading {
  page: string;
  title: string;
  marks: string;
}

/** Heading text without the status marks, e.g. "Dump a full analyzer configuration". */
export function headingTitle(raw: string): string {
  return raw
    .replace(/[✅❌]|⚠️|⚠/gu, '')
    .replace(/`/g, '')
    .replace(/\(partly\)/g, '')
    .replace(/\s+\/\s*$/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

function usageHeadings(): Heading[] {
  const dir = path.join(config().docsDir, 'usage');
  const found: Heading[] = [];
  for (const page of readdirSync(dir).filter((f) => f.endsWith('.md')).sort()) {
    for (const line of readFileSync(path.join(dir, page), 'utf8').split('\n')) {
      const m = /^#{2,3} (.+)$/.exec(line);
      if (!m) continue;
      const marks = (m[1].match(/[✅❌]|⚠️|⚠/gu) ?? []).join('');
      found.push({ page, title: headingTitle(m[1]), marks });
    }
  }
  return found;
}

function testFiles(dir: string): string[] {
  const files: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) files.push(...testFiles(full));
    else if (entry.name.endsWith('.test.ts')) files.push(full);
  }
  return files;
}

function referencedUseCases(): Array<{ page: string; title: string; file: string }> {
  const refs: Array<{ page: string; title: string; file: string }> = [];
  const re = /useCase\(\s*'([^']+\.md)',\s*'((?:[^'\\]|\\.)+)'/g;
  for (const file of testFiles(path.resolve(__dirname))) {
    const text = readFileSync(file, 'utf8');
    for (const m of text.matchAll(re)) {
      refs.push({ page: m[1], title: m[2].replace(/\\'/g, "'").replace(/`/g, ''), file: path.basename(file) });
    }
  }
  return refs;
}

const headings = usageHeadings();
const refs = referencedUseCases();
const covered = new Set(refs.map((r) => `${r.page}#${r.title}`));
const useCases = headings.filter((h) => h.marks !== '');

describe('docs/usage coverage', () => {
  test('every useCase() names an existing heading', () => {
    const known = new Set(headings.map((h) => `${h.page}#${h.title}`));
    const unknown = refs.filter((r) => !known.has(`${r.page}#${r.title}`)).map((r) => `${r.file}: ${r.page} '${r.title}'`);
    expect(unknown).toEqual([]);
  });

  test('the docs have use cases', () => {
    expect(useCases.length).toBeGreaterThan(100);
  });

  for (const h of useCases) {
    if (!covered.has(`${h.page}#${h.title}`)) {
      test.todo(`${h.page}: ${h.title} ${h.marks}`);
    }
  }
});
