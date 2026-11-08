<template>
  <div class="cc-reports" @click="openReportOfRow">
    <OriginalReports />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/views/Reports.vue (see vite.config.mjs). It shows
// the original page, and a click on an empty spot of a report row opens
// that report, as the row's file link does. The wrapper <div> itself has
// no styles, so the page lays out as before; the styles below redesign the
// File Tree view.

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

<style lang="scss" scoped>
// The File Tree view: a card with a table-like header, and the numbers of
// reports as soft pills in the color of their column. A pill is a link: it
// lists the reports of that file or folder with that severity or status.

// The columns after the name, in the order of the original template: all
// reports, the severities, then the review statuses.
$stat-columns: (
  2: rgb(var(--v-theme-primary)),
  3: #9932cc,
  4: #669603,
  5: #a9d323,
  6: #ffa800,
  7: #e92625,
  8: var(--color-soft-blue),
  9: var(--color-soft-red),
  10: var(--color-gray-light),
  11: var(--color-soft-green)
);
// The first column of each group, which a line sets apart.
$group-starts: (3, 8);
// Each number column, and the pill in it: room for six digits.
$column-width: 60px;
$pill-width: 52px;
$pill-margin: ($column-width - $pill-width) * .5;

.cc-reports :deep(.tree-view-container) {
  height: auto;
  max-height: calc(100vh - 150px);
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;

  // The header stays at the top while the tree scrolls.
  .tree-header {
    min-height: 40px;
    padding: 0 12px 0 48px;
    border-bottom: 1px solid var(--cc-border);
    background: rgb(var(--v-theme-surface));
    color: var(--cc-muted);
    font-size: .72rem;
    font-weight: 650;
    text-transform: uppercase;
    letter-spacing: .06em;
  }

  .tree-header-cell {
    width: $column-width;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 2px;
    height: 28px;
    border-radius: var(--cc-radius-sm);
    transition: background-color .15s ease, color .15s ease;

    &.sortable:hover {
      background: rgba(var(--v-theme-primary), .06);
    }

    // The column the tree is sorted by.
    &:has(.sort-icon) {
      background: rgba(var(--v-theme-primary), .1);
      color: rgb(var(--v-theme-primary));
    }

    .v-avatar {
      border-radius: 4px !important;
      font-size: .58rem;
    }
  }

  .v-treeview {
    padding: 6px;
    background: transparent;
  }

  // On a narrow pane, the tree scrolls sideways instead of pushing the
  // numbers of deep rows out of their columns: room for the ten number
  // columns, the name and about ten levels.
  .tree-header,
  .v-treeview {
    min-width: 10 * $column-width + 340px;
  }

  .v-treeview-item {
    --list-indent-size: 16px;

    min-height: 34px !important;
    margin: 1px 0;
    padding-inline-end: 6px !important;
    border-radius: var(--cc-radius-sm);

    &:hover > .v-list-item__overlay {
      opacity: .045;
    }
  }

  .v-list-item__prepend {
    > .v-list-item-action .v-btn {
      width: 26px;
      height: 26px;
      color: var(--cc-muted);
    }

    > .v-icon {
      font-size: 20px;
      color: var(--cc-muted);
      opacity: 1;
    }

    > .v-icon.mdi-folder,
    > .v-icon.mdi-folder-open {
      color: rgb(var(--v-theme-primary));
    }
  }

  .v-list-item__spacer {
    width: 8px !important;
  }

  // The group lines reach out of the row; the name cuts itself.
  .v-treeview-item .v-list-item__content,
  .v-treeview-item .v-list-item-title {
    overflow: visible;
  }

  // Names shrink (with "…") rather than push the numbers of deeper rows out
  // of their columns.
  .tree-header-name,
  .tree-item-label {
    min-width: 0;
  }

  .tree-item-label {
    font-size: .85rem;

    &.clickable:hover {
      text-decoration: none;
    }
  }

  .v-treeview-item:has(.mdi-folder, .mdi-folder-open) .tree-item-label {
    font-weight: 600;
  }

  // A pill, centered in its column.
  .tree-stat-cell {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: $pill-width;
    height: 22px;
    margin: 0 $pill-margin;
    padding: 0 4px;
    overflow: hidden;
    border-radius: 999px;
    white-space: nowrap;
    font-size: .75rem;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    transition: background-color .15s ease, transform .15s var(--cc-ease-out);

    &.clickable:hover {
      text-decoration: none;
      transform: translateY(-1px);
    }
  }

  @each $column, $color in $stat-columns {
    .tree-row > .tree-stat-cell:nth-child(#{$column}).clickable {
      background: color-mix(in srgb, #{$color} 14%, transparent);
      color: color-mix(in srgb, #{$color} 72%, rgb(var(--v-theme-on-surface)));

      &:hover {
        background: color-mix(in srgb, #{$color} 26%, transparent);
      }
    }
  }

  // All reports: a stronger pill.
  .tree-row > .tree-stat-cell:nth-child(2) {
    font-weight: 700;
  }

  // A line before each group of columns, through the header and the rows,
  // on the left edge of the column: the edge of a header cell, and the
  // pill's margin away from a pill.
  @each $column in $group-starts {
    .tree-header > :nth-child(#{$column}),
    .tree-row > :nth-child(#{$column}) {
      position: relative;
      overflow: visible;

      &::before {
        content: "";
        position: absolute;
        width: 1px;
        background: var(--cc-border);
        pointer-events: none;
      }
    }

    .tree-header > :nth-child(#{$column})::before {
      top: -6px;
      bottom: -6px;
      left: 0;
    }

    .tree-row > :nth-child(#{$column})::before {
      top: -7px;
      bottom: -7px;
      left: -$pill-margin;
    }
  }
}
</style>

