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
import { missingRequirements, type Requirement } from './requirements';

export interface UseCaseOptions {
  /** Distinguishes several tests for the same use case. */
  name?: string;
  /** Number (or "H1"-style id) of the related known-issues.md entry. */
  knownIssue?: number | string;
  /** Set when the port fixes the known issue and the test expects the fix. */
  fixed?: boolean;
  /** Optional tools the test needs; without them it is skipped. */
  requires?: Requirement[];
  timeout?: number;
}

/** Pages of docs/usage that have use cases. */
export type UsagePage = `${string}.md`;

export function useCaseTitle(heading: string, options: UseCaseOptions = {}): string {
  let title = heading;
  if (options.name) title += ` — ${options.name}`;
  if (options.knownIssue !== undefined) {
    title += options.fixed ? ` [fixed: known-issues #${options.knownIssue}]` : ` [known-issues #${options.knownIssue}]`;
  }
  return title;
}

export function useCase(
  _page: UsagePage,
  heading: string,
  fn: () => Promise<void> | void,
  options: UseCaseOptions = {},
): void {
  const title = useCaseTitle(heading, options);
  const missing = missingRequirements(options.requires ?? []);
  if (missing.length > 0) {
    test.skip(`${title} (skipped: needs ${missing.join(', ')})`, async () => {
      await fn();
    });
  } else {
    test(title, async () => {
      await fn();
    }, options.timeout);
  }
}
