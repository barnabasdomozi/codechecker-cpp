// Declares tests for the use cases in docs/usage.
//
//     describe('usage/parse.md', () => {
//       useCase('parse.md', 'Print the bug path', async () => { ... });
//     });
//
// The first two arguments must be string literals: tests/doc-coverage.test.ts
// reads them from the source to find documented use cases without tests.
//
// Tags in test names:
//   [known-issues #N]  the test covers an entry of docs/usage/known-issues.md
//                      and expects the documented Python 6.30.0 behavior.
//   [fixed: known-issues #N]
//                      the port fixes the entry; the test expects the fixed
//                      behavior and is expected to fail on the Python
//                      reference. Leave these out with
//                      `npx jest -t '^(?!.*\[fixed: known-issues)'`.

import { test } from '@jest/globals';
import { isAvailable, type Requirement } from './requirements';

export interface UseCaseOptions {
  /** Distinguishes several tests for the same use case. */
  name?: string;
  /** Number (or "H1"-style id) of the related known-issues.md entry. */
  knownIssue?: number | string;
  /** Set when the port fixes the known issue and the test expects the fix. */
  fixed?: boolean;
  /** Optional tools the test needs; without them it is skipped. */
  requires?: Requirement[];
}

export function useCase(_page: string, heading: string, fn: () => Promise<void>, options: UseCaseOptions = {}): void {
  let title = heading;
  if (options.name) title += ` — ${options.name}`;
  if (options.knownIssue !== undefined) {
    title += ` [${options.fixed ? 'fixed: ' : ''}known-issues #${options.knownIssue}]`;
  }
  const missing = (options.requires ?? []).filter((r) => !isAvailable(r)).map((r) => r.replace(/^(command|tool):/, ''));
  if (missing.length > 0) test.skip(`${title} (skipped: needs ${missing.join(', ')})`, fn);
  else test(title, fn);
}
