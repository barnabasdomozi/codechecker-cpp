<template>
  <div class="cc-filter">
    <OriginalReportFilter v-bind="$attrs" />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportFilter/ReportFilter.vue
// (see vite.config.mjs). It shows the original filter sidebar, with its
// logic, as a card: a sticky header with the number of reports, the preset
// chooser, and the filters as rows that open. The parts of the sidebar are
// v2 components of their own (ReportCount, ClearAllFilters, PresetMenu,
// FilterToolbar, ItemsSelected, ...); this file lays out the rest.

import OriginalReportFilter
  from "@v1/components/Report/ReportFilter/ReportFilter";

// Props and events go to the original component.
defineOptions({ inheritAttrs: false });
</script>

<style lang="scss" scoped>
.cc-filter {
  margin: 0 4px 16px 0;
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
}

// Help icons stay in the background until they are pointed at.
.cc-filter :deep(.v-icon.mdi-help-circle) {
  margin-left: 4px;
  font-size: 16px;
  color: var(--cc-muted) !important;
  opacity: .7;
  transition: opacity .2s ease, color .2s ease;

  &:hover {
    color: rgb(var(--v-theme-primary)) !important;
    opacity: 1;
  }
}

// The sections of the original sidebar draw their own lines.
.cc-filter > :deep(div > .v-divider) {
  display: none;
}

// The header: the number of reports and Clear all. It stays at the top
// while the filters scroll.
.cc-filter > :deep(div > div:first-child) {
  position: sticky;
  top: 0;
  z-index: 3;
  gap: 12px;
  margin: 0 !important;
  padding: 14px 14px 14px 16px;
  border-bottom: 1px solid var(--cc-border);
  border-radius: var(--cc-radius-lg) var(--cc-radius-lg) 0 0;
  background: rgb(var(--v-theme-surface));
}

// The filter preset: one row with the chooser (Filters/PresetMenu.vue).
// The original label, help icon and preset buttons are hidden; the
// chooser's menu clicks the buttons, which open the original save dialog.
.cc-filter > :deep(div > .v-list-item) {
  min-height: 0 !important;
  padding: 10px 12px !important;
  border-bottom: 1px solid var(--cc-border);
  border-radius: 0;

  > .v-list-item__content {
    display: flex;
    align-items: center;
    overflow: visible;

    > :not(.cc-preset) {
      display: none !important;
    }
  }
}

// Unique reports.
.cc-filter > :deep(div > div:not([class]) > .v-checkbox) {
  padding: 4px 12px 4px 8px;
  border-bottom: 1px solid var(--cc-border);

  .v-label {
    font-size: .85rem;
    font-weight: 600;
    opacity: 1;
  }
}

// The filters. A filter is a row that opens (FilterToolbar); the groups
// (baseline, compare to, dates) are boxes of such rows.
.cc-filter > :deep(div > .v-list) {
  padding: 8px;
  background: transparent;

  > .v-list-item {
    min-height: 0 !important;
    margin: 1px 0;
    padding: 0 !important;
    border-radius: var(--cc-radius-sm);

    > .v-list-item__content {
      overflow: visible;
    }
  }

  > .v-divider {
    display: none;
  }

  > .v-list-item.border-sm {
    margin: 6px 0;
    border: 1px solid var(--cc-border) !important;
    border-radius: var(--cc-radius) !important;
  }

  // Baseline and Compare to are the two sides of a comparison: one card in
  // two halves, with "vs" between their icons.
  > .v-list-item.border-sm:has(+ .v-list-item.border-t-0) {
    margin-bottom: 0;
    border-bottom-right-radius: 0 !important;
    border-bottom-left-radius: 0 !important;
  }

  > .v-list-item.border-sm + .v-list-item.border-t-0 {
    margin-top: 0;
    overflow: visible;
    border-top: 1px dashed var(--cc-border) !important;
    border-top-left-radius: 0 !important;
    border-top-right-radius: 0 !important;

    &::before {
      content: "vs";
      position: absolute;
      top: 0;
      left: 45px;
      z-index: 1;
      padding: 0 7px;
      border: 1px solid var(--cc-border);
      border-radius: 999px;
      background: rgb(var(--v-theme-surface));
      color: var(--cc-muted);
      font-size: .66rem;
      font-weight: 650;
      line-height: 16px;
      letter-spacing: .04em;
      transform: translate(-50%, -50%);
    }
  }

  .v-list-item.border-sm {
    .v-expansion-panel {
      background: transparent;
      color: inherit;

      &::after {
        display: none;
      }
    }

    .v-expansion-panel__shadow {
      display: none;
    }

    // The head: the chevron, an icon, the name and a line about the group.
    .v-expansion-panel-title,
    .v-expansion-panel--active > .v-expansion-panel-title {
      gap: 8px;
      min-height: 50px !important;
      padding: 6px 10px 6px 6px !important;
      border-radius: var(--cc-radius);
      color: inherit;
      font-size: .85rem !important;

      > .expansion-btn {
        order: 0;
        margin: 0;
        font-size: 18px;
        color: var(--cc-muted);
      }

      // The original icons point down and up; these point right and down,
      // as the chevrons of the filters.
      > .expansion-btn.mdi-chevron-down {
        transform: rotate(-90deg);
      }

      > .expansion-btn.mdi-chevron-up {
        transform: rotate(180deg);
      }

      // The group's icon, in the place of the spacer.
      > .v-spacer {
        display: flex;
        flex: 0 0 26px;
        order: 1;
        align-items: center;
        justify-content: center;
        height: 26px;
        border-radius: 8px;
        background: rgba(var(--v-theme-primary), .1);
        color: rgb(var(--v-theme-primary));
        transition: background-color .2s ease, color .2s ease;

        &::before {
          font: normal 16px/1 "Material Design Icons";
        }
      }

      // The name in sentence case ("BASELINE" -> "Baseline").
      > span {
        display: block;
        flex: 1 1 auto;
        order: 2;
        min-width: 0;
        margin: 0 !important;
        line-height: 1.3;

        b {
          font-weight: 600;
          text-transform: lowercase;
        }

        &::first-letter {
          text-transform: uppercase;
        }

        &::after {
          display: block;
          overflow: hidden;
          color: var(--cc-muted);
          font-size: .72rem;
          font-weight: 450;
          white-space: nowrap;
          text-overflow: ellipsis;
          text-transform: none;
        }
      }
    }

    // The rows of a group hang from a thin line under the chevron.
    .v-expansion-panel-text__wrapper {
      margin: 0 0 6px 15px;
      padding: 0 4px 0 2px !important;
      border-left: 1px solid var(--cc-border);
    }

    // A group opens at once and its content fades in, as a filter does
    // (FilterToolbar.vue): Vuetify's animated height lays out the page in
    // every frame.
    .expand-transition-enter-active,
    .expand-transition-leave-active {
      transition-duration: 0s !important;
    }

    .v-expansion-panel--active > .v-expansion-panel-text
      > .v-expansion-panel-text__wrapper {
      animation: cc-filter-open .18s var(--cc-ease-out) both;
    }

    .v-divider {
      display: none;
    }
  }

  // A group with a filter set: a filled icon and the primary color, also
  // while the group is closed.
  .v-list-item.border-sm:has(.cc-filter-badge, .selected-items)
    .v-expansion-panel-title {
    > .v-spacer {
      background: rgb(var(--v-theme-primary));
      color: rgb(var(--v-theme-on-primary));
    }

    > span b {
      color: rgb(var(--v-theme-primary));
    }
  }

  // The icon and the line of each group. They find the group by a filter
  // in it.
  .v-list-item.border-sm:has(#run) .v-expansion-panel-title {
    > .v-spacer::before { content: "\F09FE"; } // mdi-layers-outline
    > span::after { content: "The runs to list reports from"; }
  }

  .v-list-item.border-sm:has(#newcheck) .v-expansion-panel-title {
    > .v-spacer::before { content: "\F1492"; } // mdi-compare-horizontal
    > span::after { content: "Runs to diff against the baseline"; }
  }

  .v-list-item.border-sm:has(#detection-date-filter)
    .v-expansion-panel-title {
    > .v-spacer::before { content: "\F0B68"; } // mdi-calendar-range-outline
    > span::after { content: "When reports were detected or fixed"; }
  }
}

// Remove filtered reports, at the bottom.
.cc-filter > :deep(div > div.justify-center) {
  margin: 0 !important;
  padding: 12px 16px 16px;
  border-top: 1px solid var(--cc-border);

  > * {
    margin: 0 !important;
  }
}

// "Unavailable filters are hidden for this page."
.cc-filter > :deep(div > .v-alert) {
  margin: 0 12px 12px !important;
  padding: 8px 12px;
  border-radius: var(--cc-radius-sm);
  font-size: .78rem !important;

  .v-alert__prepend {
    margin-inline-end: 8px;

    .v-icon {
      font-size: 18px;
    }
  }
}
</style>
