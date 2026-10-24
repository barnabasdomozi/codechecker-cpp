// The test projects in testing/analyze-targets, and the steps to build, log and
// analyze them.

import { cpSync, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { config } from './config';
import { cc, describeResult, run, sh, type RunResult } from './run';
import type { Sandbox } from './sandbox';
import { shared } from './shared';

export interface ProjectDef {
  /** Folder under testing/analyze-targets. */
  source: string;
  /** Folder name of the copy (the docs use ~/demo and ~/demo2). */
  copyAs: string;
  /** Run before `log`, not logged (e.g. CMake's configure step). */
  prepare?: string;
  /** The build command given to `CodeChecker log -b`. */
  build: string;
  /** Exit code of `log` (the build's exit code). */
  logExitCode: number;
}

export const PROJECTS = {
  demo: { source: 'demo/v1', copyAs: 'demo', build: 'make', logExitCode: 0 },
  demo2: { source: 'demo/v2', copyAs: 'demo2', build: 'make', logExitCode: 0 },
  'cmake-app': {
    source: 'cmake-app',
    copyAs: 'cmake-app',
    prepare: 'cmake -S . -B build',
    build: 'cmake --build build',
    logExitCode: 0,
  },
  'make-recursive': { source: 'make-recursive', copyAs: 'make-recursive', build: 'make', logExitCode: 0 },
  ctu: { source: 'ctu', copyAs: 'ctu', build: 'make', logExitCode: 0 },
  'review-status': { source: 'review-status', copyAs: 'review-status', build: 'make', logExitCode: 0 },
  'build-failures': { source: 'build-failures', copyAs: 'build-failures', build: 'make -k', logExitCode: 2 },
  clean: { source: 'clean', copyAs: 'clean', build: 'make', logExitCode: 0 },
  'odd-paths': { source: 'odd-paths', copyAs: 'odd-paths', build: './build.sh', logExitCode: 0 },
  'analyzer-features': { source: 'analyzer-features', copyAs: 'analyzer-features', build: 'make', logExitCode: 0 },
  'external-tools': { source: 'external-tools', copyAs: 'external-tools', build: 'make', logExitCode: 0 },
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
  const source = PROJECTS[name].source.split('/')[0];
  return path.join(config().projectsDir, source, 'README.md');
}

/** Copies a project into `parent/<copyAs>` and runs its prepare step. */
export async function copyProject(name: ProjectName, parent: string): Promise<string> {
  const def: ProjectDef = PROJECTS[name];
  const root = path.join(parent, def.copyAs);
  mkdirSync(parent, { recursive: true });
  cpSync(path.join(config().projectsDir, def.source), root, { recursive: true });
  if (def.prepare) {
    const result = await sh(def.prepare, { cwd: root });
    if (result.exitCode !== 0) throw new Error(`Preparing ${name} failed:\n${describeResult(result)}`);
  }
  return root;
}

/** Copies a project into a new sandbox folder. */
export function projectCopy(box: Sandbox, name: ProjectName): Promise<string> {
  return copyProject(name, box.newDir(name));
}

/** `CodeChecker log -b <build> -o compile_commands.json`, checking the exit code. */
export async function logProject(name: ProjectName, root: string, extraArgs: string[] = []): Promise<RunResult> {
  const def: ProjectDef = PROJECTS[name];
  const result = await cc(['log', '-b', def.build, '-o', 'compile_commands.json', ...extraArgs], { cwd: root });
  if (result.exitCode !== def.logExitCode || !existsSync(path.join(root, 'compile_commands.json'))) {
    throw new Error(`Logging the build of ${name} failed:\n${describeResult(result)}`);
  }
  return result;
}

export interface Analysis {
  /** The project copy. */
  root: string;
  /** The report folder. */
  reports: string;
  /** compile_commands.json */
  compileDb: string;
  analyzeExitCode: number | null;
}

/**
 * Copies, logs and analyzes a project in `parent` (with DOC_ANALYZERS, then
 * `analyzeArgs`). The report folder is `<root>/<reportsName>`, like
 * `~/demo/reports` in the docs.
 */
export async function analyzeProjectIn(
  name: ProjectName,
  parent: string,
  analyzeArgs: string[] = [],
  options: { reportsName?: string; expectExit?: number[] } = {},
): Promise<Analysis> {
  const root = await copyProject(name, parent);
  await logProject(name, root);
  const reportsName = options.reportsName ?? 'reports';
  const result = await cc(['analyze', 'compile_commands.json', '-o', reportsName, ...DOC_ANALYZERS, ...analyzeArgs], { cwd: root });
  const allowed = options.expectExit ?? [0];
  if (result.exitCode === null || !allowed.includes(result.exitCode)) {
    throw new Error(`Analyzing ${name} failed:\n${describeResult(result)}`);
  }
  return {
    root,
    reports: path.join(root, reportsName),
    compileDb: path.join(root, 'compile_commands.json'),
    analyzeExitCode: result.exitCode,
  };
}

/** Like analyzeProjectIn, in a new sandbox folder. */
export function analyzeProject(
  box: Sandbox,
  name: ProjectName,
  analyzeArgs: string[] = [],
  options: { reportsName?: string; expectExit?: number[] } = {},
): Promise<Analysis> {
  return analyzeProjectIn(name, box.newDir(`${name}-analysis`), analyzeArgs, options);
}

/**
 * An analysis shared by every test file of the session. **Read-only**: copy
 * it (see copyAnalysis) before changing anything.
 */
export async function sharedAnalysis(
  name: ProjectName,
  analyzeArgs: string[] = [],
  options: { variant?: string; expectExit?: number[] } = {},
): Promise<Analysis> {
  const key = `analysis-${name}-${options.variant ?? (analyzeArgs.join('_') || 'default')}`;
  const { value } = await shared(key, async (dir) => analyzeProjectIn(name, dir, analyzeArgs, options));
  return value;
}

/** Runs a command and fails with its output if the exit code is not `expected`. */
export async function mustRun(
  promise: Promise<RunResult>,
  expected: number | number[] = 0,
): Promise<RunResult> {
  const result = await promise;
  const allowed = Array.isArray(expected) ? expected : [expected];
  if (result.exitCode === null || !allowed.includes(result.exitCode)) {
    throw new Error(`Unexpected exit code (expected ${allowed.join(' or ')}):\n${describeResult(result)}`);
  }
  return result;
}

/** Writes a compilation database. */
export function writeCompileDb(file: string, entries: Array<{ directory: string; command?: string; arguments?: string[]; file: string }>): string {
  writeFileSync(file, JSON.stringify(entries, null, 2));
  return file;
}

/** Runs the project's own Makefile target or script, without CodeChecker. */
export function buildWithoutLogging(root: string, command: string): Promise<RunResult> {
  return run('/bin/sh', ['-c', command], { cwd: root });
}
