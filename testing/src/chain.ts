// Command chains: several CodeChecker commands run one after another, each
// working on what the previous steps produced.
//
//     const flow = chain('log, analyze, parse');
//     flow.step('log', async () => { ... });
//     flow.step('analyze', async () => { ... });
//
// Every step is its own test, so a failure points at the step that caused
// it. When a step fails, the later steps are not run: they fail at once with
// a "not run" message naming the failed step. (Jest cannot skip a test once
// the run has started.)

import { test } from '@jest/globals';

export interface Chain {
  step(name: string, fn: () => Promise<void>, timeout?: number): void;
}

export function chain(name: string): Chain {
  let failedStep: string | undefined;
  let index = 0;
  return {
    step(stepName, fn, timeout) {
      index += 1;
      const title = `${name} — step ${index}: ${stepName}`;
      test(title, async () => {
        if (failedStep) {
          throw new Error(`NOT RUN: the earlier step '${failedStep}' failed.`);
        }
        try {
          await fn();
        } catch (error) {
          failedStep = stepName;
          throw error;
        }
      }, timeout);
    },
  };
}
