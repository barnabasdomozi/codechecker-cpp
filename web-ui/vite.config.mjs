import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";

const require = createRequire(import.meta.url);
const root = dirname(fileURLToPath(import.meta.url));
const src = join(root, "src");

// Generated Thrift API JavaScript stubs.
const apiDir = join(root, "..", "..", "..", "codechecker_api");
const apiJsDir = join(apiDir, "js");
const apiVersion =
  JSON.parse(readFileSync(join(apiDir, "api_version.json"))).api_version;

const apiStubs = {
  "@cc/auth": "codeCheckerAuthentication.js",
  "@cc/auth-types": "authentication_types.js",
  "@cc/conf": "configurationService.js",
  "@cc/conf-types": "configuration_types.js",
  "@cc/db-access": "codeCheckerDBAccess.js",
  "@cc/prod": "codeCheckerProductService.js",
  "@cc/prod-types": "products_types.js",
  "@cc/server-info": "serverInfoService.js",
  "@cc/report-server-types": "report_server_types.js",
  "@cc/shared-types": "shared_types.js",
};

const CC_SERVICE_ENDPOINTS = [
  "Authentication",
  "Configuration",
  "CodeCheckerService",
  "Products",
  "ServerInfo"
];

// Location of the Thrift API server for the dev server.
const CC_THRIFT_API_HOST =
  process.env.CC_THRIFT_API_HOST || "http://localhost";
const CC_THRIFT_API_PORT = process.env.CC_THRIFT_API_PORT || 8002;
const apiProxy = {
  target: CC_THRIFT_API_HOST + ":" + CC_THRIFT_API_PORT,
  changeOrigin: true,
  secure: false
};

// Thrift and node-int64 use the Node.js `Buffer` global, also while their
// modules load. Import it from the npm `buffer` package in every module that
// uses it.
const injectBuffer = {
  transform: {
    inject: { Buffer: [ "buffer", "Buffer" ] }
  }
};

export default defineConfig({
  plugins: [ vue() ],
  define: {
    "process.env.CC_API_VERSION": JSON.stringify(apiVersion),
    "process.env.CC_SERVER_HOST":
      JSON.stringify(process.env.CC_SERVER_HOST || null),
    "process.env.CC_SERVER_PORT":
      JSON.stringify(process.env.CC_SERVER_PORT || null)
  },
  resolve: {
    // The sources import components without the ".vue" extension.
    extensions: [ ".mjs", ".js", ".json", ".vue" ],
    alias: [
      { find: /^@\//, replacement: src + "/" },
      { find: "@statistics", replacement: join(src, "components", "Statistics") },
      { find: "@cc-api", replacement: join(src, "services", "api") },
      ...Object.entries(apiStubs).map(([ name, file ]) => ({
        find: new RegExp(`^${name}$`),
        replacement: join(apiJsDir, file)
      })),
      // The API stubs live outside this folder, so their bare
      // `require("thrift")` and `require("node-int64")` would not find this
      // project's node_modules.
      {
        find: /^thrift$/,
        replacement: require.resolve("thrift/lib/nodejs/lib/thrift/browser.js")
      },
      { find: /^node-int64$/, replacement: require.resolve("node-int64") },
      // Thrift needs the npm `buffer`, `events` and `util` packages, not the
      // Node.js modules.
      { find: /^buffer$/, replacement: require.resolve("buffer/") },
      { find: /^events$/, replacement: require.resolve("events/") },
      { find: /^util$/, replacement: require.resolve("util/") }
    ]
  },
  optimizeDeps: {
    // The API stubs are CommonJS outside node_modules: pre-bundle them so the
    // dev server can serve them as ES modules.
    include: Object.keys(apiStubs),
    rolldownOptions: injectBuffer
  },
  css: {
    preprocessorOptions: {
      scss: {
        quietDeps: true
      }
    }
  },
  server: {
    port: 8080,
    proxy: {
      [`^(/[^?]*)?/(${CC_SERVICE_ENDPOINTS.join("|")})(\\?.*)?$`]: apiProxy,
      "^/docs/": apiProxy
    }
  },
  build: {
    // Put the built files in the root of dist/, not in dist/assets/. The
    // server takes the first folder of a path as a product name, unless it is
    // one of a few known names, such as "images".
    assetsDir: "",
    chunkSizeWarningLimit: 2048,
    rolldownOptions: injectBuffer
  }
});
