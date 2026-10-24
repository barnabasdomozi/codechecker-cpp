// docs/usage/server.md, the server parts of config-file.md and the HTTPS
// part of store.md.

import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';
import { describe, expect } from '@jest/globals';
import { cc, run } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages } from '../../src/normalize';
import { sharedAnalysis } from '../../src/projects';
import { runs, Server, privateReports } from '../../src/server';

const box = useSandbox();

/** Starts a server, runs the test body, and always stops the server. */
async function withServer(options: Parameters<typeof Server.start>[1], body: (server: Server) => Promise<void>): Promise<void> {
  const server = await Server.start(box, options);
  try {
    await body(server);
  } finally {
    await server.stop();
  }
}

async function waitForOutput(server: Server, text: string, timeoutMs = 20_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!server.output.includes(text)) {
    if (Date.now() > deadline) throw new Error(`'${text}' did not appear in the server output:\n${server.output}`);
    await sleep(200);
  }
}

const productsList = (server: Server, url: string, env?: Record<string, string | undefined>) =>
  server.cc(['cmd', 'products', 'list', '-o', 'json', '--url', url], { env });

describe('usage/server.md', () => {
  useCase('server.md', 'Start a server', async () => {
    await withServer({}, async (server) => {
      await waitForOutput(server, 'Server waiting for client requests on');
      const info = logMessages(server.output, 'INFO');
      const ws = server.workspace;
      expect(info).toContain(`Workspace directory: ${ws}`);
      expect(info).toContain(`Config directory: ${ws}`);
      expect(info).toContain('Checking configuration database ...');
      expect(info).toContain(`CodeChecker server's example configuration file created at '${ws}/server_config.json'`);
      expect(info).toContain('Server API version: 6.74');
      expect(info).toContain(`Searching for SSL key at ${ws}/key.pem, cert at ${ws}/cert.pem, not found!`);
      expect(info).toContain('Falling back to simple, insecure HTTP.');
      expect(info).toContain(`Server waiting for client requests on [localhost:${server.port}]`);
      for (const file of ['config.sqlite', 'Default.sqlite', 'server_config.json']) {
        expect(existsSync(path.join(ws, file))).toBe(true);
      }
      // Default product created at the first start.
      expect(await productsList(server, server.serverUrl)).toExitWith(0);
      expect(server.running).toBe(true);
    });
  });

  useCase('server.md', 'Start a server', async () => {
    await withServer({}, async (server) => {
      const config = JSON.parse(readFileSync(path.join(server.workspace, 'server_config.json'), 'utf8')) as Record<string, unknown>;
      for (const key of ['worker_processes', 'background_worker_processes', 'max_run_count', 'store', 'keepalive', 'authentication']) {
        expect(config).toHaveProperty(key);
      }
      expect(config.store).toEqual({ analysis_statistics_dir: null, limit: { failure_zip_size: 52428800, compilation_database_size: 104857600 } });
      expect(config.keepalive).toEqual({ enabled: false, idle: 600, interval: 30, max_probe: 10 });
      await waitForOutput(server, 'Server waiting for client requests on');
      expect(logMessages(server.output, 'WARNING').join('\n')).toContain(`'${server.workspace}/server_config.json' is readable by users other than you!`);
      expect(logMessages(server.output, 'WARNING').some((w) => w.startsWith('Number of worker processes can not be negative!'))).toBe(true);
    });
  }, { name: 'server_config.json and its warnings', knownIssue: 12 });

  useCase('server.md', 'Listen on other addresses', async () => {
    await withServer({ args: ['--host', '127.0.0.1'] }, async (server) => {
      await waitForOutput(server, `Server waiting for client requests on [127.0.0.1:${server.port}]`);
      expect(await productsList(server, `127.0.0.1:${server.port}`)).toExitWith(0);
    });
    await withServer({ args: ['--not-host-only'] }, async (server) => {
      await waitForOutput(server, `Server waiting for client requests on [[::]:${server.port}]`);
      expect(await productsList(server, server.serverUrl)).toExitWith(0);
    });
  });

  useCase('server.md', 'Listen on other addresses', async () => {
    await withServer({ args: ['--host', '::1'], waitHost: '::1' }, async (server) => {
      await waitForOutput(server, `Server waiting for client requests on [[::1]:${server.port}]`);
    });
  }, { name: 'IPv6' });

  useCase('server.md', 'Separate config directory and config database file', async () => {
    const dir = box.newDir('separate');
    const ws = path.join(dir, 'ws2');
    const cfg = path.join(dir, 'cfg2');
    await withServer({ workspace: ws, args: ['-f', cfg, '--sqlite', path.join(ws, 'my_config.sqlite')] }, async (server) => {
      expect(await productsList(server, server.serverUrl)).toExitWith(0);
      expect(existsSync(path.join(cfg, 'server_config.json'))).toBe(true);
      expect(existsSync(path.join(ws, 'server_config.json'))).toBe(false);
      expect(existsSync(path.join(ws, 'my_config.sqlite'))).toBe(true);
      expect(existsSync(path.join(ws, 'Default.sqlite'))).toBe(true);
    });
  });

  useCase('server.md', 'Process counts and machine ID', async () => {
    const demo = await sharedAnalysis('demo');
    await withServer({ args: ['--machine-id', 'test-machine'] }, async (server) => {
      await waitForOutput(server, 'Server waiting for client requests on');
      const info = logMessages(server.output, 'INFO');
      expect(info).toContain('Using 2 API request handler processes ...');
      expect(info).toContain('Using 1 Task worker processes ...');
      const stored = await server.cc(['store', privateReports(demo.reports), '-n', 'm', '--detach', '--url', server.url()]);
      expect(stored).toExitWith(0);
      const token = lines(stored.stdout).filter((l) => l.trim()).pop()!;
      expect(await server.cc(['cmd', 'serverside-tasks', '-t', token, '--await', '--url', server.serverUrl])).toExitWith(0);
      const list = await server.cc(['cmd', 'serverside-tasks', '--status', 'completed', '--output', 'json', '--url', server.serverUrl]);
      expect(list).toExitWith(0);
      const tasks = JSON.parse(list.stdout) as Array<{ token: string; machineId: string }>;
      expect(tasks.find((t) => t.token === token)?.machineId).toBe('test-machine');
    });
  });

  useCase('server.md', 'Skip database cleanup at start', async () => {
    await withServer({ args: ['--skip-db-cleanup'] }, async (server) => {
      expect(await productsList(server, server.serverUrl)).toExitWith(0);
    });
  });

  useCase('server.md', 'HTTPS', async () => {
    const dir = box.newDir('https');
    const ws = path.join(dir, 'ws4');
    mkdirSync(ws, { recursive: true });
    const ssl = await run(
      'openssl',
      ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', path.join(ws, 'key.pem'), '-out', path.join(ws, 'cert.pem'), '-days', '365', '-subj', '/CN=localhost'],
      { cwd: dir },
    );
    expect(ssl).toExitWith(0);
    const cert = path.join(ws, 'cert.pem');
    await withServer({ workspace: ws }, async (server) => {
      await waitForOutput(server, 'Server waiting for client requests on');
      expect(logMessages(server.output, 'INFO')).toContain('Initiating SSL. Server listening on secure socket.');
      const https = `https://localhost:${server.port}`;
      expect(await productsList(server, https, { SSL_CERT_FILE: cert })).toExitWith(0);

      const untrusted = await productsList(server, https);
      expect(untrusted).toExitWith(1);
      const errors = logMessages(untrusted.output, 'ERROR').join('\n');
      expect(errors).toContain('Connection failed.');
      expect(errors).toContain('CERTIFICATE_VERIFY_FAILED');
      expect(errors).toContain('Check if your CodeChecker server is running.');
      // REQUESTS_CA_BUNDLE has no effect.
      expect(await productsList(server, https, { REQUESTS_CA_BUNDLE: cert })).toExitWith(1);
    });
  }, { requires: ['command:openssl'] });

  useCase('store.md', 'Different products and HTTPS', async () => {
    const demo = await sharedAnalysis('demo');
    const dir = box.newDir('https-store');
    const ws = path.join(dir, 'ws');
    mkdirSync(ws, { recursive: true });
    expect(
      await run('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', path.join(ws, 'key.pem'), '-out', path.join(ws, 'cert.pem'), '-days', '365', '-subj', '/CN=localhost'], { cwd: dir }),
    ).toExitWith(0);
    await withServer({ workspace: ws }, async (server) => {
      const stored = await server.cc(['store', privateReports(demo.reports), '-n', 'tlsrun', '--url', `https://localhost:${server.port}/Default`], {
        env: { SSL_CERT_FILE: path.join(ws, 'cert.pem') },
      });
      expect(stored).toExitWith(0);
    });
  }, { name: 'HTTPS', requires: ['command:openssl'] });

  useCase('server.md', 'Options from a file', async () => {
    const config = box.write('server.yaml', 'server:\n  - --skip-db-cleanup\n');
    await withServer({ args: ['--config', config] }, async (server) => {
      await waitForOutput(server, 'Server waiting for client requests on');
      expect(logMessages(server.output, 'INFO')).toContain(
        `Extending command line options with server options from '${config}' file: --skip-db-cleanup`,
      );
    });
  });

  useCase('config-file.md', 'Server settings', async () => {
    const config = box.write('server2.yaml', 'server:\n  - --skip-db-cleanup\n');
    await withServer({ args: ['--config', config] }, async (server) => {
      await waitForOutput(server, 'Server waiting for client requests on');
      const info = logMessages(server.output, 'INFO');
      expect(info.some((l) => l.startsWith('Full extended command: ') && l.includes('--skip-db-cleanup'))).toBe(true);
    });
  });

  useCase('server.md', 'List running servers', async () => {
    await withServer({}, async (server) => {
      const result = await cc(['server', '-l'], { cwd: box.dir });
      expect(result).toExitWith(0);
      expect(result.stdout).toContain('Your running CodeChecker servers:');
      expect(result.stdout).toMatch(/│ Workspace +│ View port │/);
      expect(result.stdout).toContain(`│ ${server.port} `);
      // Recorded in the user's home folder.
      expect(readFileSync(path.join(box.home, '.codechecker.instances.json'), 'utf8')).toContain(String(server.port));
    });
  });

  useCase('server.md', 'Reload configuration', async () => {
    await withServer({}, async (server) => {
      const result = await cc(['server', '-w', server.workspace, '-v', String(server.port), '-r'], { cwd: box.dir });
      expect(result).toExitWith(0);
      await waitForOutput(server, 'Server configuration reload: Done.');
      expect(server.running).toBe(true);
    });
  });

  useCase('server.md', 'Stop servers', async () => {
    const server = await Server.start(box);
    const result = await cc(['server', '-w', server.workspace, '-v', String(server.port), '-s'], { cwd: box.dir });
    expect(result).toExitWith(0);
    expect(logMessages(result.output, 'INFO').some((l) =>
      new RegExp(`^Stopped CodeChecker server running on port ${server.port} in workspace ${server.workspace} \\(PID: \\d+\\)$`).test(l),
    )).toBe(true);
    const exit = await server.waitForExit();
    expect(exit.code).toBe(143);

    const a = await Server.start(box);
    const b = await Server.start(box);
    expect(await cc(['server', '--stop-all'], { cwd: box.dir })).toExitWith(0);
    await a.waitForExit();
    await b.waitForExit();
    // Nothing to stop: still 0.
    expect(await cc(['server', '--stop-all'], { cwd: box.dir })).toExitWith(0);
  });

  useCase('server.md', 'Exit codes', async () => {
    const server = await Server.start(box);
    const exit = await server.stop();
    expect(exit.code).toBe(143);

    // Port in use: a second server on the same port fails to start.
    const first = await Server.start(box);
    try {
      const second = await cc(['server', '-w', path.join(box.newDir('second'), 'ws'), '-v', String(first.port)], { cwd: box.dir, timeoutMs: 60_000 });
      expect(second).toExitWith(1);
    } finally {
      await first.stop();
    }
  });

  useCase('server.md', 'Start a server', async () => {
    // runs works against the started server; also proves the API is ready.
    await withServer({}, async (server) => {
      expect(await runs(server)).toEqual({});
    });
  }, { name: 'a new server has no runs' });
});
