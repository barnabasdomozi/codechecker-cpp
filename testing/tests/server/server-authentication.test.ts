// docs/usage/server-authentication.md

import { chmodSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect } from '@jest/globals';
import { cc } from '../../src/run';
import { useSandbox } from '../../src/sandbox';
import { useCase } from '../../src/usecase';
import { lines, logMessages } from '../../src/normalize';
import { sharedAnalysis } from '../../src/projects';
import { Server, privateReports } from '../../src/server';

const box = useSandbox();
let authServer: Server;
const server = () => authServer;

const AUTH = {
  enabled: true,
  super_user: 'admin',
  realm_name: 'CodeChecker Privileged server',
  realm_error: 'Access requires valid credentials.',
  session_lifetime: 300,
  refresh_time: 60,
  logins_until_cleanup: 30,
  max_pers_auth_token_expiration_length: 365,
  method_dictionary: { enabled: true, auths: ['admin:admin123', 'bob:bob123'], groups: { bob: ['devs'] } },
};

/** Turns on authentication in a workspace's generated server_config.json. */
function enableAuthentication(workspace: string): void {
  const file = path.join(workspace, 'server_config.json');
  const config = JSON.parse(readFileSync(file, 'utf8')) as Record<string, Record<string, unknown>>;
  config.authentication = { ...config.authentication, ...AUTH };
  writeFileSync(file, JSON.stringify(config, null, 2));
  chmodSync(file, 0o600);
}

beforeAll(async () => {
  // The first start creates server_config.json; turn on authentication in
  // it and start the server again.
  const first = await Server.start(box);
  await first.stop();
  enableAuthentication(first.workspace);
  authServer = await Server.start(box, { workspace: first.workspace });
});

/** Each user gets its own session file. */
function sessionEnv(user: string, extra: Record<string, string | undefined> = {}): Record<string, string | undefined> {
  return { CC_SESSION_FILE: path.join(box.dir, `session-${user}.json`), ...extra };
}

const login = (user: string, env: Record<string, string | undefined>, input?: string) =>
  server().cc(['cmd', 'login', user, '--url', server().serverUrl], { env, input });

describe('usage/server-authentication.md', () => {
  useCase('server-authentication.md', 'Turn on authentication', async () => {
    const result = await server().cc(['cmd', 'runs', '--url', server().url()], { env: sessionEnv('nobody') });
    expect(result).toExitWith(1);
    const errors = logMessages(result.output, 'ERROR');
    expect(errors).toContain('Invalid pre-configured credentials.');
    expect(errors).toContain(
      `Your password has been changed or personal access token has been removed which is used by your "${box.home}/.codechecker.passwords.json" file. Please remove or change invalid credentials.`,
    );
    // The password file does not exist, yet the message blames it.
    expect(existsSync(path.join(box.home, '.codechecker.passwords.json'))).toBe(false);
  }, { knownIssue: 11 });

  useCase('server-authentication.md', 'Turn on authentication', async () => {
    // The reload succeeds, but authentication stays off until the server is
    // restarted (usage/server-authentication.md).
    const open = await Server.start(box);
    enableAuthentication(open.workspace);
    expect(await cc(['server', '-w', open.workspace, '-v', String(open.port), '-r'], { cwd: box.dir })).toExitWith(0);
    await open.waitForOutput('Server configuration reload: Done.');
    expect(await open.cc(['cmd', 'runs', '--url', open.url()], { env: sessionEnv('reloaded') })).toExitWith(0);
    await open.stop();
  }, { name: 'a reload does not turn it on' });

  useCase('server-authentication.md', 'Force authentication from the command line', async () => {
    const forced = await Server.start(box, { args: ['--force-authentication'] });
    await forced.waitForOutput('Server waiting for client requests');
    expect(logMessages(forced.output, 'INFO')).toContain(
      "'--force-authentication' was passed as a command-line option. The server will ask for users to authenticate!",
    );
    expect(logMessages(forced.output, 'WARNING').some((w) => w.startsWith('Authentication was manually enabled, but no valid authentication backends are configured'))).toBe(true);
    expect(await forced.cc(['cmd', 'runs', '--url', forced.url()], { env: sessionEnv('forced') })).toExitWith(1);
    await forced.stop();
  });

  useCase('server-authentication.md', 'Log in', async () => {
    const env = sessionEnv('admin-stdin');
    const result = await login('admin', env, 'admin123\n');
    expect(result).toExitWith(0);
    expect(result.output).toContain("Please provide password for user 'admin':");
    expect(logMessages(result.output, 'INFO')).toContain('Server reported successful authentication.');
    expect(existsSync(env.CC_SESSION_FILE!)).toBe(true);
    // Later calls use the saved session.
    expect(await server().cc(['cmd', 'runs', '--url', server().url()], { env })).toExitWith(0);
  });

  useCase('server-authentication.md', 'Log in', async () => {
    const env = sessionEnv('admin-env', { CC_PASSWORD: 'admin123' });
    const result = await login('admin', env);
    expect(result).toExitWith(0);
    const info = logMessages(result.output, 'INFO');
    expect(info).toContain('Using credentials from CC_PASSWORD env var to log in.');
    expect(info).toContain('Logging in using preconfigured credentials...');
    expect(info).toContain('Server reported successful authentication.');
  }, { name: 'CC_PASSWORD' });

  useCase('server-authentication.md', 'Log in', async () => {
    const result = await login('bob', sessionEnv('bob-wrong', { CC_PASSWORD: 'wrong' }));
    expect(result).toExitWith(1);
    expect(logMessages(result.output, 'ERROR')).toContain('Calling API endpoint: performLogin');
    expect(result.output).toContain("Invalid credentials supplied for user 'bob'. Refusing authentication!");
  }, { name: 'wrong password' });

  useCase('server-authentication.md', 'Log in', async () => {
    // Without CC_SESSION_FILE the session goes to ~/.codechecker.session.json.
    const result = await login('admin', { CC_PASSWORD: 'admin123' });
    expect(result).toExitWith(0);
    expect(readFileSync(path.join(box.home, '.codechecker.session.json'), 'utf8')).toContain(`localhost:${server().port}`);
  }, { name: 'the default session file' });

  useCase('server-authentication.md', 'Log out', async () => {
    const env = sessionEnv('logout', { CC_PASSWORD: 'admin123' });
    for (const flag of ['-d', '--deactivate', '--logout']) {
      expect(await login('admin', env)).toExitWith(0);
      const result = await server().cc(['cmd', 'login', flag, '--url', server().serverUrl], { env: sessionEnv('logout') });
      expect(result).toExitWith(0);
      expect(logMessages(result.output, 'INFO')).toContain('Successfully logged out.');
      expect(await server().cc(['cmd', 'runs', '--url', server().url()], { env: sessionEnv('logout') })).toExitWith(1);
    }
  });

  useCase('server-authentication.md', 'Automatic login with a password file', async () => {
    const demo = await sharedAnalysis('demo');
    const adminEnv = sessionEnv('store-admin', { CC_PASSWORD: 'admin123' });
    expect(await login('admin', adminEnv)).toExitWith(0);
    expect(await server().cc(['store', privateReports(demo.reports), '-n', 'authrun', '--url', server().url()], { env: adminEnv })).toExitWith(0);

    for (const key of [`localhost:${server().port}`, 'localhost', `*:${server().port}`, '*']) {
      const file = box.write(`passwords-${key.replace(/\W/g, '_')}.json`, JSON.stringify({ client_autologin: true, credentials: { [key]: 'admin:admin123' } }));
      chmodSync(file, 0o600);
      const env = sessionEnv(`pass-${key.replace(/\W/g, '_')}`, { CC_PASS_FILE: file });
      const result = await server().cc(['cmd', 'runs', '-o', 'csv', '--url', server().url()], { env });
      expect(result).toExitWith(0);
      expect(logMessages(result.output, 'INFO')).toContain('Authentication successful.');
      expect(lines(result.stdout).some((l) => l.startsWith('authrun,6,'))).toBe(true);
    }
  });

  useCase('server-authentication.md', 'Automatic login with a password file', async () => {
    const file = path.join(box.home, '.codechecker.passwords.json');
    writeFileSync(file, JSON.stringify({ client_autologin: true, credentials: { '*': 'admin:admin123' } }));
    chmodSync(file, 0o600);
    try {
      const result = await server().cc(['cmd', 'runs', '--url', server().url()], { env: sessionEnv('home-pass') });
      expect(result).toExitWith(0);
    } finally {
      writeFileSync(file, JSON.stringify({ client_autologin: false, credentials: {} }));
    }
  }, { name: 'the default password file' });

  useCase('server-authentication.md', 'Personal access tokens', async () => {
    const env = sessionEnv('tokens', { CC_PASSWORD: 'admin123' });
    expect(await login('admin', env)).toExitWith(0);
    const created = await server().cc(['cmd', 'token', 'new', 'ci_token', '--description', 'CI token', '--expiration', '30', '--url', server().serverUrl], { env });
    expect(created).toExitWith(0);
    const token = /The following access token has been generated for your account: ([0-9a-f]{32})/.exec(created.stdout)?.[1];
    expect(token).toBeDefined();
    expect(created.stdout).toMatch(/The token is valid until: \d{4}-\d\d-\d\d \d\d:\d\d:\d\d/);

    const table = await server().cc(['cmd', 'token', 'list', '--url', server().serverUrl], { env });
    expect(table).toExitWith(0);
    expect(table.stdout).toMatch(/│ Name +│ Description +│ Last access +│ Expiration +│/);
    expect(table.stdout).toMatch(/│ ci_token +│ CI token +│/);
    const json = await server().cc(['cmd', 'token', 'list', '-o', 'json', '--url', server().serverUrl], { env });
    const tokens = JSON.parse(json.stdout) as Array<Record<string, string>>;
    // The token value is not shown again.
    expect(tokens.find((t) => t.name === 'ci_token')).toMatchObject({ token: '', name: 'ci_token', description: 'CI token' });

    // Logging in with the token instead of the password.
    const withToken = sessionEnv('token-login', { CC_PASSWORD: token });
    expect(await login('admin', withToken)).toExitWith(0);
    const passFile = box.write('token-pass.json', JSON.stringify({ client_autologin: true, credentials: { '*': `admin:${token}` } }));
    chmodSync(passFile, 0o600);
    expect(await server().cc(['cmd', 'runs', '--url', server().url()], { env: sessionEnv('token-file', { CC_PASS_FILE: passFile }) })).toExitWith(0);

    const removed = await server().cc(['cmd', 'token', 'del', 'ci_token', '--url', server().serverUrl], { env });
    expect(removed).toExitWith(0);
    expect(removed.output).toContain("'ci_token' has been successfully removed.");
    const after = await login('admin', sessionEnv('token-after', { CC_PASSWORD: token }));
    expect(after).toExitWith(1);
    expect(after.output).toContain('Invalid credentials');
  });

  useCase('server-authentication.md', 'Permissions', async () => {
    const env = sessionEnv('perm-admin', { CC_PASSWORD: 'admin123' });
    expect(await login('admin', env)).toExitWith(0);
    const result = await server().cc(['cmd', 'permissions', '--url', server().serverUrl], { env });
    expect(result).toExitWith(0);
    const perms = JSON.parse(result.stdout) as { version: number; global_permissions: { user_permissions: Record<string, string[]>; group_permissions: Record<string, string[]> }; product_permissions: Record<string, unknown> };
    expect(perms.version).toBe(1);
    expect(perms.global_permissions.user_permissions).toEqual({ admin: ['SUPERUSER'] });
    expect(perms.global_permissions.group_permissions).toEqual({});

    const bob = sessionEnv('perm-bob', { CC_PASSWORD: 'bob123' });
    expect(await login('bob', bob)).toExitWith(0);
    const denied = await server().cc(['cmd', 'products', 'add', 'bobprod', '--url', server().serverUrl], { env: bob });
    expect(denied).toExitWith(1);
    expect(logMessages(denied.output, 'ERROR')).toEqual(expect.arrayContaining(['Unauthorized to access', 'Ask the product admin for additional access rights.']));
    expect(denied.output).toContain('You are not authorized to execute this action.');
    // A user without listing rights gets an empty task list.
    const bobTasks = await server().cc(['cmd', 'serverside-tasks', '--status', 'completed', '--output', 'json', '--url', server().serverUrl], { env: bob });
    expect(JSON.parse(bobTasks.stdout)).toEqual([]);
  });

  useCase('server-authentication.md', 'Permissions', async () => {
    const open = await Server.start(box);
    const result = await open.cc(['cmd', 'permissions', '--url', open.serverUrl], { env: sessionEnv('open') });
    expect(result).toExitWith(0);
    expect(result.output).toContain(`No valid token or session was found for localhost:${open.port}`);
    const perms = JSON.parse(result.stdout.slice(result.stdout.indexOf('{'))) as { global_permissions: { user_permissions: Record<string, unknown> } };
    expect(perms.global_permissions.user_permissions).toEqual({});
    await open.stop();
  }, { name: 'a server without authentication' });
});
