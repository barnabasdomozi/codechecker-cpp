# View results in a browser

A running server also serves a web application for browsing the stored
results:

```
http://localhost:8001/                 # product list
http://localhost:8001/Default/         # runs of the "Default" product
```

✅ Both URLs return HTTP `200` on the test server. The web UI itself was not
tested interactively.

The web application is a Vue app (source in
`web/server/vue-cli/src`, built files in `www/` of the package). Its pages
(router paths):

| Path | Page |
|---|---|
| `/` | Product list (`Products.vue`) |
| `/login`, `/login/OAuthLogin/:provider` | Login, OAuth login |
| `/:endpoint/runs` | Runs and run history (`RunList.vue`, `RunDetail.vue`) |
| `/:endpoint/reports` | Report list with filters (`Reports.vue`) |
| `/:endpoint/report-detail` | One report on its source code with the bug path (`ReportDetail.vue`) |
| `/:endpoint/statistics` (`overview`, `checker`, `severity`, `component`, `guideline`, `coverage`) | Statistics (`Statistics.vue`) |
| `/:endpoint/source-component` | Source component management (`SourceComponent.vue`) |
| `/:endpoint/cleanup-plan` | Cleanup plans (`CleanupPlan.vue`) |
| `/:endpoint/review-status-rules` | Server-side review status rules (`ReviewStatusRules.vue`) |
| `/userguide`, `/new-features` | Built-in documentation |

Product details (`ProductDetail.vue`) cover product settings and permissions.

The UI uses the same Thrift API as `CodeChecker cmd`. The C++ server needs to
serve the existing `www/` files and implement the API. Features that exist
only in the UI (review status rules, cleanup plans, comments, setting review
status by hand) still need API support in the port.
