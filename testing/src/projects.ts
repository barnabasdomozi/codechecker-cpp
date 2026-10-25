// The test projects in testing/analyze-targets, and the steps to build, log and
// analyze them.

import { cpSync, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { config } from './config';
import { cc, describeResult, run, sh, type RunResult } from './run';
import type { Sandbox } from './sandbox';
import { shared } from './shared';

interface ProjectDef {
  /** Folder under testing/analyze-targets. The copy is named after the project (the docs use ~/demo and ~/demo2). */
  source: string;
  /** Run before `log`, not logged (e.g. CMake's configure step). */
  prepare?: string;
  /** The build command given to `CodeChecker log -b`. */
  build: string;
  /** Exit code of `log` (the build's exit code), if not 0. */
  logExitCode?: number;
}

export const PROJECTS = {
  demo: { source: 'demo/v1', build: 'make' },
  demo2: { source: 'demo/v2', build: 'make' },
  'cmake-app': { source: 'cmake-app', prepare: 'cmake -S . -B build', build: 'cmake --build build' },
  'make-recursive': { source: 'make-recursive', build: 'make' },
  ctu: { source: 'ctu', build: 'make' },
  'review-status': { source: 'review-status', build: 'make' },
  'build-failures': { source: 'build-failures', build: 'make -k', logExitCode: 2 },
  clean: { source: 'clean', build: 'make' },
  'odd-paths': { source: 'odd-paths', build: './build.sh' },
  'analyzer-features': { source: 'analyzer-features', build: 'make' },
  'external-tools': { source: 'external-tools', build: 'make' },
} satisfies Record<string, ProjectDef>;

export type ProjectName = keyof typeof PROJECTS;

/**
 * The analyzers of the machine the docs and the project READMEs were
 * written on: Clang SA, Clang-Tidy, GCC and Cppcheck (no Infer). Commands
 * whose results are compared with documented results pass this, so that
 * the results are the same on machines with other analyzers installed.
 * Options given after it (e.g. a test's own --analyzers) win.
 */
export const DOC_ANALYZERS = ['--analyzers', 'clangsa', 'clang-tidy', 'gcc', 'cppcheck'];

/** The README of a project, with its expected results. */
export function projectReadme(name: ProjectName): string {
  return path.join(config().projectsDir, PROJECTS[name].source.split('/')[0], 'README.md');
}

/** docs/reference/demo-project.md, with the expected results of demo and demo2. */
export function demoProjectDoc(): string {
  return path.join(config().docsDir, 'reference', 'demo-project.md');
}

/** Runs a command and fails with its output if the exit code is not `expected`. */
async function mustRun(promise: Promise<RunResult>, expected: number | number[] = 0): Promise<RunResult> {
  const result = await promise;
  const allowed = Array.isArray(expected) ? expected : [expected];
  if (result.exitCode === null || !allowed.includes(result.exitCode)) {
    throw new Error(`Unexpected exit code (expected ${allowed.join(' or ')}):\n${describeResult(result)}`);
  }
  return result;
}

/** Copies a project into `parent/<name>` and runs its prepare step. */
export async function copyProject(name: ProjectName, parent: string): Promise<string> {
  const def: ProjectDef = PROJECTS[name];
  const root = path.join(parent, name);
  mkdirSync(parent, { recursive: true });
  cpSync(path.join(config().projectsDir, def.source), root, { recursive: true });
  if (def.prepare) await mustRun(sh(def.prepare, { cwd: root }));
  return root;
}

/** Copies a project into a new sandbox folder. */
export function projectCopy(box: Sandbox, name: ProjectName): Promise<string> {
  return copyProject(name, box.newDir(name));
}

/** `CodeChecker log -b <build> -o compile_commands.json`, checking the exit code. */
async function logProject(name: ProjectName, root: string): Promise<void> {
  const def: ProjectDef = PROJECTS[name];
  await mustRun(cc(['log', '-b', def.build, '-o', 'compile_commands.json'], { cwd: root }), def.logExitCode ?? 0);
  if (!existsSync(path.join(root, 'compile_commands.json'))) throw new Error(`Logging the build of ${name} wrote no compile_commands.json`);
}

/** Copies a project into a new sandbox folder and logs its build. */
export async function loggedProject(box: Sandbox, name: ProjectName): Promise<string> {
  const root = await projectCopy(box, name);
  await logProject(name, root);
  return root;
}

export interface Analysis {
  /** The project copy. */
  root: string;
  /** The report folder, `<root>/reports` like `~/demo/reports` in the docs. */
  reports: string;
  /** compile_commands.json */
  compileDb: string;
  analyzeExitCode: number | null;
}

/** Copies, logs and analyzes a project in `parent` (with DOC_ANALYZERS, then `analyzeArgs`). */
async function analyzeProjectIn(name: ProjectName, parent: string, analyzeArgs: string[], expectExit: number): Promise<Analysis> {
  const root = await copyProject(name, parent);
  await logProject(name, root);
  const result = await mustRun(cc(['analyze', 'compile_commands.json', '-o', 'reports', ...DOC_ANALYZERS, ...analyzeArgs], { cwd: root }), expectExit);
  return {
    root,
    reports: path.join(root, 'reports'),
    compileDb: path.join(root, 'compile_commands.json'),
    analyzeExitCode: result.exitCode,
  };
}

/** Copies, logs and analyzes a project in a new sandbox folder. */
export function analyzeProject(box: Sandbox, name: ProjectName, analyzeArgs: string[] = []): Promise<Analysis> {
  return analyzeProjectIn(name, box.newDir(`${name}-analysis`), analyzeArgs, 0);
}

/** An analysis shared by every test file of the session. **Read-only.** */
export function sharedAnalysis(name: ProjectName, analyzeArgs: string[] = [], expectExit = 0): Promise<Analysis> {
  const key = `analysis-${name}-${analyzeArgs.join('_') || 'default'}`;
  return shared(key, (dir) => analyzeProjectIn(name, dir, analyzeArgs, expectExit));
}

/**
 * A copy of external-tools after `make`, with the output of the other tools
 * in out/. Shared by every test file of the session. **Read-only.**
 */
export function builtExternalTools(): Promise<string> {
  return shared('external-tools-output', async (dir) => {
    const root = await copyProject('external-tools', dir);
    await mustRun(run('make', [], { cwd: root }));
    return root;
  });
}

/** Writes a compilation database. */
export function writeCompileDb(file: string, entries: Array<{ directory: string; command?: string; arguments?: string[]; file: string }>): string {
  writeFileSync(file, JSON.stringify(entries, null, 2));
  return file;
}
