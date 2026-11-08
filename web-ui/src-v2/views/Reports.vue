<template>
  <div class="cc-reports" @click="openReportOfRow">
    <OriginalReports />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/views/Reports.vue (see vite.config.mjs). It shows
// the original page, and a click on an empty spot of a report row opens
// that report, as the row's file link does. The wrapper <div> has no
// styles, so the page lays out as before.

import OriginalReports from "@v1/views/Reports";

const REPORT_ROW = ".reports-table tbody > tr.v-data-table__tr";

// Clicks on these keep their own meaning, such as the checker name, which
// opens the checker's documentation.
const INTERACTIVE =
  "a, button, input, label, select, textarea, [role='button'], " +
  ".v-selection-control, .v-chip, .v-btn, .checker-name";

function openReportOfRow(event) {
  const row = event.target.closest(REPORT_ROW);
  if (!row || event.target.closest(INTERACTIVE)) return;

  // Selecting text in a row does not open the report.
  if (window.getSelection()?.toString()) return;

  const link = row.querySelector("a[href*='report-detail']");
  if (!link) return;

  if (event.ctrlKey || event.metaKey) {
    window.open(link.href, "_blank");
  } else {
    link.click();
  }
}
</script>

