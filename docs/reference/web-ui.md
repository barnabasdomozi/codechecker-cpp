# Reference: the web UI

Usage: [../usage/web-ui.md](../usage/web-ui.md)

Python/JS source: `web/server/vue-cli/src` (Vue application; built into the
package's `www/`), especially `router/index.js`,
`services/api/_base.service.js`, `services/api/token.service.js`,
`store/modules/auth.js`; server side `web/server/codechecker_server/server.py`
(`do_GET`).

The web UI is a static single-page application. The port does not need to
re-implement it: it must serve the existing built files and implement the
API calls the UI makes, with the same wire format
([server-api.md](server-api.md)).

## 1. Serving

See [server-api.md](server-api.md) §2.3: files from `<package>/www`,
`/<product>/...` prefix removed once, unknown paths → `index.html`, `/live`
and `/ready` probes, no directory listings, security headers. The UI's
routes therefore all load `index.html`:

| Route | Page |
|---|---|
| `/` | product list |
| `/login`, `/login/OAuthLogin/:provider` | login; OAuth callback |
| `/userguide`, `/new-features`, `/404` | static pages |
| `/:endpoint` (endpoint `[A-Za-z0-9_-]+`), `/:endpoint/runs` | runs and history |
| `/:endpoint/statistics/{overview,checker,severity,component,coverage,guideline}` | statistics |
| `/:endpoint/reports`, `/:endpoint/report-detail` | report list, one report |
| `/:endpoint/cleanup-plan`, `/:endpoint/review-status-rules`, `/:endpoint/source-component` | management pages |
| anything else | not-found page |

## 2. How the UI calls the API

- Thrift JSON protocol over `XMLHttpRequest` POST to
  `[/<endpoint>]/v<API version baked in at build time>/<Service>`, host and
  port from the page location, HTTPS if the page is HTTPS. Service names:
  `Authentication`, `Configuration`, `ServerInfo`, `Products`,
  `CodeCheckerService` (the product-specific client is re-created when the
  route's endpoint changes).
- Every request carries `Authorization: Bearer <token>`; the token is kept
  in the browser's `localStorage` under the key `__ccPrivilegedAccessToken`
  (set after `performLogin`, removed on logout or on a 401).
- An error whose message contains `Error code 401:` (the server's
  "unauthorized" `TApplicationException`, [server-api.md](server-api.md)
  §1.2) clears the token and redirects to `/login?return_to=<current path>`.
  An HTTP 401 status does the same; HTTP 504 shows an error.
- A message matching `The product .* does not exist!` redirects to `/404`.
- Other errors are shown as notifications with their message text, so the
  server's error messages are user-visible.
- Login: `performLogin("Username:Password", "<user>:<password>")`, or for
  OAuth `getOauthProviders()`, `createLink(provider)` (browser goes to the
  provider), then on `/login/OAuthLogin/<provider>` `performLogin("oauth",
  "<provider>@<full callback URL>")`
  ([server-authentication.md](server-authentication.md) §2.5). After login:
  `getLoggedInUser()`; start-up also calls `getAuthParameters()`,
  `getPackageVersion()` (ServerInfo) and `getNotificationBannerText()`.

## 3. API calls used by the UI

A port must implement all of these (grouped by service; areas in which the
UI uses them in parentheses). Calls also used by the CLI are specified on
the CLI reference pages; the others follow the IDL comments and the Python
handlers in `report_server.py`, `product_server.py`, `authentication.py`.

**Authentication**: `getAuthParameters`, `getLoggedInUser`,
`performLogin`, `destroySession`, `getOauthProviders`, `createLink`,
`hasPermission`, `getPermissionsForUser`, `getAuthorisedNames`,
`addPermission`, `removePermission` (product permission dialogs),
`getPersonalAccessTokens`, `newPersonalAccessToken`,
`removePersonalAccessToken`, `getMaxTokenExpiration` (token dialog).

**Configuration**: `getNotificationBannerText`,
`setNotificationBannerText` (banner, superuser).

**ServerInfo**: `getPackageVersion`.

**Products**: `getProducts`, `getProductCount`, `getCurrentProduct`,
`getProductConfiguration`, `addProduct`, `editProduct`, `removeProduct`,
`isAdministratorOfAnyProduct`.

**CodeCheckerService**:
- runs: `getRunData`, `getRunCount`, `getRunHistory`,
  `getRunHistoryTagCounts`, `removeRun`, `getAnalysisInfo`,
  `getAnalysisStatistics`, `getFailedFiles`, `getFailedFilesCount`;
- reports: `getRunResults`, `getRunResultCount`, `getReport`,
  `getReportDetails`, `getReportAnnotations`, `getSourceFileData`,
  `getBlameInfo`, `removeRunReports`, `getFileCountsSummary`;
- filter counts: `getCheckerCounts`, `getCheckerMsgCounts`,
  `getAnalyzerNameCounts`, `getSeverityCounts`, `getReviewStatusCounts`,
  `getDetectionStatusCounts`, `getReportStatusCounts`, `getFileCounts`,
  `getRunReportCounts`;
- statistics: `getCheckerStatusVerificationDetails`, `getCheckerLabels`,
  `getGuidelineRules`;
- comments: `getComments`, `getCommentCount`, `addComment`,
  `updateComment`, `removeComment`;
- review status rules: `addReviewStatusRule`, `getReviewStatusRules`,
  `getReviewStatusRulesCount`, `removeReviewStatusRules`;
- components: `getSourceComponents`, `addSourceComponent`,
  `removeSourceComponent`;
- cleanup plans: `getCleanupPlans`, `addCleanupPlan`, `updateCleanupPlan`,
  `removeCleanupPlan`, `closeCleanupPlan`, `reopenCleanupPlan`,
  `setCleanupPlan`, `unsetCleanupPlan`;
- filter presets: `listFilterPreset`, `getFilterPreset`,
  `storeFilterPreset`, `renameFilterPreset`, `deleteFilterPreset`.

Review statuses set in the UI go through `addReviewStatusRule` (a rule per
hash, applied to every report with that hash that has no source-comment
status, with a system comment; refused when the product has
"review status change disabled"), not through `changeReviewStatus`.

## 4. Text the UI interprets

- System comments are stored as `rev_st_changed <Old> <New>` /
  `rev_st_changed_msg <Old> <New> <quoted message>`
  ([cmd-export-import.md](cmd-export-import.md) §2) and rendered by the
  server from templates in `config/system_comment_kinds.json` when sent
  with report details (HTML-escaped).
- The banner text is plain text transported as base64.
- Display names and descriptions of products are base64 in the API.
