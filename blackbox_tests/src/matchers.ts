// Custom Jest matchers. Loaded through `setupFilesAfterEnv`.

import { expect } from '@jest/globals';
import { describeResult, type RunResult } from './run';

declare module 'expect' {
  interface Matchers<R extends void | Promise<void>, T = unknown> {
    /**
     * The command exited with this code (or one of these codes). On failure
     * the message shows the command and its output.
     */
    toExitWith(expected: number | number[]): R;
    /** The command's output (stdout + stderr) contains the text. */
    toOutput(expected: string | RegExp): R;
  }
}

function isRunResult(value: unknown): value is RunResult {
  return typeof value === 'object' && value !== null && 'exitCode' in value && 'command' in value;
}

expect.extend({
  toExitWith(received: unknown, expected: number | number[]) {
    if (!isRunResult(received)) {
      return { pass: false, message: () => 'toExitWith expects the result of run()/cc()' };
    }
    const allowed = Array.isArray(expected) ? expected : [expected];
    const pass = received.exitCode !== null && allowed.includes(received.exitCode);
    return {
      pass,
      message: () =>
        `expected exit code ${this.isNot ? 'not ' : ''}${allowed.join(' or ')}, got ${received.exitCode}\n${describeResult(received)}`,
    };
  },
  toOutput(received: unknown, expected: string | RegExp) {
    if (!isRunResult(received)) {
      return { pass: false, message: () => 'toOutput expects the result of run()/cc()' };
    }
    const pass = typeof expected === 'string' ? received.output.includes(expected) : expected.test(received.output);
    return {
      pass,
      message: () =>
        `expected the output ${this.isNot ? 'not ' : ''}to contain ${String(expected)}\n${describeResult(received)}`,
    };
  },
});
