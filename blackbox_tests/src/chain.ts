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

export function chain(name: string) {
  let failedStep: string | undefined;
  let index = 0;
  return {
    step(stepName: string, fn: () => Promise<void>): void {
      index += 1;
      test(`${name} — step ${index}: ${stepName}`, async () => {
        if (failedStep) throw new Error(`NOT RUN: the earlier step '${failedStep}' failed.`);
        try {
          await fn();
        } catch (error) {
          failedStep = stepName;
          throw error;
        }
      });
    },
  };
}
