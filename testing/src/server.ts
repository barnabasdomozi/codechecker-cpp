// Starts and stops CodeChecker servers for the server use cases.

import { spawn, type ChildProcess } from 'node:child_process';
import { cpSync, mkdirSync, mkdtempSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { config, sessionDir } from './config';
import { beforeAll } from '@jest/globals';
import { buildEnv, cc, describeResult, type RunOptions, type RunResult } from './run';
import type { Sandbox } from './sandbox';

/** A free TCP port on localhost. */
export function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      if (address && typeof address === 'object') {
        const { port } = address;
        server.close(() => resolve(port));
      } else {
        server.close(() => reject(new Error('Could not get a free port')));
      }
    });
  });
}

function canConnect(port: number, host: string): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = net.connect({ port, host });
    socket.once('connect', () => {
      socket.destroy();
      resolve(true);
    });
    socket.once('error', () => resolve(false));
  });
}

export interface ServerOptions {
  /** Workspace folder; default `<new sandbox folder>/ws`. */
  workspace?: string;
  /** Extra `CodeChecker server` arguments. */
  args?: string[];
  env?: Record<string, string | undefined>;
  /** Host to wait for; default localhost (127.0.0.1). */
  waitHost?: string;
  /** Use the default (many) worker processes instead of small counts. */
  defaultProcesses?: boolean;
}

export class Server {
  private exitInfo: { code: number | null; signal: NodeJS.Signals | null } | undefined;
  private exited: Promise<void>;
  output = '';

  private constructor(
    readonly process: ChildProcess,
    readonly port: number,
    readonly workspace: string,
    readonly cwd: string,
    readonly env: Record<string, string | undefined>,
  ) {
    this.exited = new Promise((resolve) => {
      // 'exit', not 'close': the server's worker processes may keep the
      // output pipes open a little longer.
      process.on('exit', (code, signal) => {
        this.exitInfo = { code, signal };
        resolve();
      });
    });
    process.stdout?.setEncoding('utf8');
    process.stderr?.setEncoding('utf8');
    process.stdout?.on('data', (c: string) => (this.output += c));
    process.stderr?.on('data', (c: string) => (this.output += c));
  }

  /**
   * Starts `CodeChecker server -w <ws> -v <free port>` and waits until the
   * port accepts connections.
   */
  static async start(box: Sandbox, options: ServerOptions = {}): Promise<Server> {
    const cwd = box.newDir('server');
    const workspace = options.workspace ?? path.join(cwd, 'ws');
    mkdirSync(workspace, { recursive: true });
    const port = await freePort();
    const args = ['server', '-w', workspace, '-v', String(port)];
    if (!options.defaultProcesses) {
      args.push('--api-handler-processes', '2', '--task-worker-processes', '1');
    }
    args.push(...(options.args ?? []));
    const env = options.env ?? {};
    const child = spawn(config().codechecker, args, {
      cwd,
      env: buildEnv(env),
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    const server = new Server(child, port, workspace, cwd, env);
    // Never leave a server running after the test file, even if a test did
    // not stop it.
    box.onCleanup(() => server.stop().then(() => undefined));
    await server.waitUntilReady(options.waitHost ?? '127.0.0.1');
    return server;
  }

  private async waitUntilReady(host: string): Promise<void> {
    const deadline = Date.now() + 120_000;
    while (Date.now() < deadline) {
      if (this.exitInfo) {
        throw new Error(
          `The server exited before it was ready (code ${this.exitInfo.code}, signal ${this.exitInfo.signal}):\n${this.output}`,
        );
      }
      if (await canConnect(this.port, host)) {
        // The port opens a little before the API is fully ready.
        await sleep(300);
        return;
      }
      await sleep(250);
    }
    this.process.kill('SIGKILL');
    throw new Error(`The server did not open port ${this.port} in time:\n${this.output}`);
  }

  /** `localhost:<port>` (a server URL, for products and tasks). */
  get serverUrl(): string {
    return `localhost:${this.port}`;
  }

  /** `localhost:<port>/<product>` (a product URL). */
  url(product = 'Default'): string {
    return `localhost:${this.port}/${product}`;
  }

  /** Runs a `CodeChecker` client command with the server's environment. */
  cc(args: string[], options: Partial<RunOptions> = {}): Promise<RunResult> {
    return cc(args, { cwd: options.cwd ?? this.cwd, ...options, env: { ...this.env, ...options.env } });
  }

  get running(): boolean {
    return this.exitInfo === undefined;
  }

  /** Waits until the process exits; returns its exit status. */
  async waitForExit(timeoutMs = 60_000): Promise<{ code: number | null; signal: NodeJS.Signals | null }> {
    const timer = new AbortController();
    await Promise.race([this.exited, sleep(timeoutMs, undefined, { signal: timer.signal }).catch(() => undefined)]);
    timer.abort();
    if (!this.exitInfo) throw new Error(`The server did not exit in ${timeoutMs} ms`);
    return this.exitInfo;
  }

  /** Stops the server with SIGTERM (SIGKILL if it does not stop). */
  async stop(): Promise<{ code: number | null; signal: NodeJS.Signals | null }> {
    if (this.exitInfo) return this.exitInfo;
    this.process.kill('SIGTERM');
    try {
      return await this.waitForExit(30_000);
    } catch {
      this.process.kill('SIGKILL');
      return this.waitForExit(10_000);
    }
  }
}

// ---------------------------------------------------------------------------
// Client helpers.

/** Every review and detection status, for listing all reports. */
export const ALL_RESULTS_FILTER = [
  '--review-status',
  'unreviewed',
  'confirmed',
  'false_positive',
  'intentional',
  '--detection-status',
  'new',
  'resolved',
  'unresolved',
  'reopened',
  'off',
  'unavailable',
];

/** Thrift enum values used in the JSON output of `cmd results`. */
export const DETECTION_STATUS: Record<number, string> = {
  0: 'new',
  1: 'resolved',
  2: 'unresolved',
  3: 'reopened',
  4: 'off',
  5: 'unavailable',
};
export const REVIEW_STATUS: Record<number, string> = {
  0: 'unreviewed',
  1: 'confirmed',
  2: 'false_positive',
  3: 'intentional',
};

/** One report of `cmd results -o json`. */
export interface ServerReport {
  runId: number;
  checkerId: string;
  bugHash: string;
  checkedFile: string;
  checkerMsg: string;
  line: number;
  column: number;
  severity: number;
  reviewData: { status: number; comment: string; author: string; isInSource: boolean };
  detectionStatus: number;
  detectedAt: string;
  fixedAt: string | null;
  bugPathLength: number;
  analyzerName: string;
  runName: string;
  [key: string]: unknown;
}

/**
 * A private copy of a report folder. `store` writes temporary files into
 * the report folder, so parallel stores of one shared folder would see each
 * other's files. The copy still points to the same (unchanged) sources.
 */
export function privateReports(reports: string): string {
  const dir = mkdtempSync(path.join(sessionDir(), 'f', 'rc-'));
  const copy = path.join(dir, path.basename(reports));
  cpSync(reports, copy, { recursive: true, preserveTimestamps: true });
  return copy;
}

/**
 * `CodeChecker store <folders> -n <name> --url <product>` (with private
 * copies of the folders); fails on a non-zero exit.
 */
export async function store(server: Server, folders: string[], name: string, extra: string[] = [], product = 'Default'): Promise<RunResult> {
  const copies = folders.map(privateReports);
  const result = await server.cc(['store', ...copies, '-n', name, '--url', server.url(product), ...extra]);
  if (result.exitCode !== 0) throw new Error(`store failed:\n${describeResult(result)}`);
  return result;
}

/** `cmd results <runs> -o json`, with the given filters. */
export async function results(server: Server, runs: string[], filters: string[] = [], product = 'Default'): Promise<ServerReport[]> {
  const result = await server.cc(['cmd', 'results', ...runs, '-o', 'json', '--url', server.url(product), ...filters]);
  if (result.exitCode !== 0) throw new Error(`cmd results failed:\n${describeResult(result)}`);
  return result.stdout.trim() === '' ? [] : (JSON.parse(result.stdout) as ServerReport[]);
}

/** `<bugHash> <detection status> <review status>` of every report of a run, sorted. */
export async function runState(server: Server, run: string, product = 'Default'): Promise<string[]> {
  const reports = await results(server, [run], ALL_RESULTS_FILTER, product);
  return reports
    .map((r) => `${r.bugHash} ${DETECTION_STATUS[r.detectionStatus]} ${REVIEW_STATUS[r.reviewData.status]}`)
    .sort();
}

/** Run names and data from `cmd runs -o json`. */
export async function runs(server: Server, args: string[] = [], product = 'Default'): Promise<Record<string, Record<string, unknown>>> {
  const result = await server.cc(['cmd', 'runs', '-o', 'json', '--url', server.url(product), ...args]);
  if (result.exitCode !== 0) throw new Error(`cmd runs failed:\n${describeResult(result)}`);
  const list = JSON.parse(result.stdout) as Array<Record<string, Record<string, unknown>>>;
  return Object.assign({}, ...list) as Record<string, Record<string, unknown>>;
}

/** Starts a server for the current test file and stops it at the end. */
export function useServer(box: Sandbox, options: ServerOptions = {}): () => Server {
  let server: Server | undefined;
  beforeAll(async () => {
    server = await Server.start(box, options);
  });
  return () => {
    if (!server) throw new Error('The server is used before beforeAll ran.');
    return server;
  };
}
