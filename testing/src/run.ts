// Runs commands and collects their output.

import { spawn } from 'node:child_process';
import { config, toolPath } from './config';

export interface RunOptions {
  cwd: string;
  /** Variables added to (or, with undefined, removed from) the environment. */
  env?: Record<string, string | undefined>;
  /** Text written to standard input. Without it, stdin is /dev/null. */
  input?: string;
  /** Kill the command after this many milliseconds (default 10 minutes). */
  timeoutMs?: number;
}

export interface RunResult {
  /** The command line, for messages. */
  command: string[];
  cwd: string;
  /** Exit code, or null if the process was killed by a signal. */
  exitCode: number | null;
  signal: NodeJS.Signals | null;
  stdout: string;
  stderr: string;
  /** stdout and stderr in the order they arrived. */
  output: string;
  timedOut: boolean;
}

/** Environment every command gets, before `RunOptions.env`. */
let baseEnv: Record<string, string | undefined> = {};

/**
 * Sets variables that every command of the current test file gets, e.g. an
 * isolated HOME. See sandbox.ts.
 */
export function setBaseEnv(env: Record<string, string | undefined>): void {
  baseEnv = { ...env };
}

export function buildEnv(extra: Record<string, string | undefined> = {}): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = { ...process.env };
  for (const [key, value] of Object.entries({ ...baseEnv, ...extra })) {
    if (value === undefined) delete env[key];
    else env[key] = value;
  }
  return env;
}

export function run(program: string, args: string[], options: RunOptions): Promise<RunResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(program, args, {
      cwd: options.cwd,
      env: buildEnv(options.env),
      stdio: [options.input === undefined ? 'ignore' : 'pipe', 'pipe', 'pipe'],
    });
    let stdout = '';
    let stderr = '';
    let output = '';
    let timedOut = false;
    child.stdout!.setEncoding('utf8');
    child.stderr!.setEncoding('utf8');
    child.stdout!.on('data', (chunk: string) => {
      stdout += chunk;
      output += chunk;
    });
    child.stderr!.on('data', (chunk: string) => {
      stderr += chunk;
      output += chunk;
    });
    if (options.input !== undefined) {
      child.stdin!.on('error', () => {
        // The command may exit before reading its input.
      });
      child.stdin!.end(options.input);
    }
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, options.timeoutMs ?? 600_000);
    child.on('error', (error) => {
      clearTimeout(timer);
      reject(error);
    });
    child.on('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({
        command: [program, ...args],
        cwd: options.cwd,
        exitCode,
        signal,
        stdout,
        stderr,
        output,
        timedOut,
      });
    });
  });
}

/** Runs the `CodeChecker` under test. */
export function cc(args: string[], options: RunOptions): Promise<RunResult> {
  return run(config().codechecker, args, options);
}

/** Runs a standalone tool shipped next to `CodeChecker`. */
export function tool(name: string, args: string[], options: RunOptions): Promise<RunResult> {
  const program = toolPath(name);
  if (!program) {
    throw new Error(`The build has no '${name}' next to ${config().codechecker}.`);
  }
  return run(program, args, options);
}

/** Runs a shell command line with /bin/sh. */
export function sh(commandLine: string, options: RunOptions): Promise<RunResult> {
  return run('/bin/sh', ['-c', commandLine], options);
}

/** A short description of a result, for failure messages. */
export function describeResult(result: RunResult, maxChars = 6000): string {
  const tail = (text: string) =>
    text.length > maxChars ? `...(${text.length - maxChars} chars cut)...\n${text.slice(-maxChars)}` : text;
  return [
    `command: ${result.command.map(quoteArg).join(' ')}`,
    `cwd: ${result.cwd}`,
    `exit code: ${result.exitCode}${result.signal ? ` (signal ${result.signal})` : ''}${result.timedOut ? ' (timed out)' : ''}`,
    `--- stdout ---\n${tail(result.stdout)}`,
    `--- stderr ---\n${tail(result.stderr)}`,
  ].join('\n');
}

function quoteArg(arg: string): string {
  return /^[\w@%+=:,./-]+$/.test(arg) ? arg : `'${arg.replace(/'/g, `'\\''`)}'`;
}
