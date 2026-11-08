import { existsSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";

const require = createRequire(import.meta.url);
const root = dirname(fileURLToPath(import.meta.url));
const src = join(root, "src");
const srcV2 = join(root, "src-v2");

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

// Web UI v2 (`vite build --mode v2`, served by `server --web-ui-v2`). It is
// the same application with another design: src-v2/ is an overlay of src/.
// When a module of src/ is imported, the build uses the file with the same
// path under src-v2/ instead, if there is one. "@v1/<path>" imports the
// original src/<path> even if src-v2/ replaces it.
function webUiV2Overlay() {
  return {
    name: "cc-web-ui-v2-overlay",
    enforce: "pre",
    async resolveId(source, importer, options) {
      if (source.startsWith("@v1/")) {
        const original = join(src, source.slice("@v1/".length));
        return this.resolve(original, importer, { ...options, skipSelf: true });
      }

      const resolved =
        await this.resolve(source, importer, { ...options, skipSelf: true });
      if (!resolved || resolved.external) return resolved;

      // Keep a query such as "?vue&type=style" after the file name.
      const queryStart = resolved.id.indexOf("?");
      const file =
        queryStart < 0 ? resolved.id : resolved.id.slice(0, queryStart);
      const query = queryStart < 0 ? "" : resolved.id.slice(queryStart);
      if (!file.startsWith(src + sep)) return resolved;

      // A .vue file loading its own blocks (<style>, ...) keeps them, also
      // when it was imported with "@v1/".
      if (importer?.split("?")[0] === file) return resolved;

      const replacement = join(srcV2, relative(src, file));
      return existsSync(replacement) ? replacement + query : resolved;
    },
    // Marks the page as the v2 UI and shows a splash screen until the app
    // is mounted over it.
    transformIndexHtml(html) {
      const splash =
        readFileSync(join(srcV2, "splash.html"), "utf-8").trim();
      return html
        .replace("<html lang=\"en\">", "<html lang=\"en\" data-web-ui=\"v2\">")
        .replace("<div id=\"app\"></div>", `<div id="app">${splash}</div>`);
    }
  };
}

export default defineConfig(({ mode }) => ({
  plugins: mode === "v2" ? [ webUiV2Overlay(), vue() ] : [ vue() ],
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
      { find: /^@v2\//, replacement: srcV2 + "/" },
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
    outDir: mode === "v2" ? "dist-v2" : "dist",
    // Put the built files in the root of dist/, not in dist/assets/. The
    // server takes the first folder of a path as a product name, unless it is
    // one of a few known names, such as "images".
    assetsDir: "",
    chunkSizeWarningLimit: 2048,
    rolldownOptions: injectBuffer
  }
}));
