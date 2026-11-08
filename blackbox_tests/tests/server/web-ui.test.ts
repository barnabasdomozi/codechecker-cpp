// docs/usage/web-ui.md: the server serves the web application.

import { describe, expect, test } from '@jest/globals';
import { useSandbox } from '../../src/sandbox';
import { useServer } from '../../src/server';

const box = useSandbox();
const server = useServer(box);

describe('usage/web-ui.md', () => {
  test('the product list and a product page return HTTP 200', async () => {
    for (const page of ['/', '/Default/']) {
      const response = await fetch(`http://localhost:${server().port}${page}`);
      expect(response.status).toBe(200);
      expect(response.headers.get('content-type') ?? '').toContain('text/html');
      const html = await response.text();
      expect(html).toContain('<html');
      // The original design unless the server runs with --web-ui-v2.
      expect(html).not.toContain('data-web-ui="v2"');
    }
  });
});
