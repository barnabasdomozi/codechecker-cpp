<template>
  <div class="cc-filter">
    <OriginalReportFilter v-bind="$attrs" />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportFilter/ReportFilter.vue
// (see vite.config.mjs). It shows the original filter sidebar, with its
// logic, as a card: a sticky header with the number of reports, the preset,
// and the filters as rows that open. The parts of the sidebar are v2
// components of their own (ReportCount, ClearAllFilters, FilterToolbar,
// ItemsSelected, ...); this file lays out the rest.

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

// The filter preset: a label, the preset list and its buttons.
.cc-filter > :deep(div > .v-list-item) {
  min-height: 0 !important;
  padding: 12px 16px 14px !important;
  border-bottom: 1px solid var(--cc-border);
  border-radius: 0;

  > .v-list-item__content {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px 2px;
    overflow: visible;

    > span:first-child {
      margin-right: 4px !important;
      font-size: .72rem !important;
      font-weight: 650 !important;
      text-transform: uppercase;
      letter-spacing: .06em;
      color: var(--cc-muted);
    }

    > .v-input {
      flex: 1 1 100%;
    }

    // The buttons side by side, lighter than the page's main buttons.
    > div {
      flex: 1 1 100%;
      flex-direction: row !important;
      gap: 8px;
      margin: 0 !important;
    }

    .v-btn {
      flex: 1 1 0;
      height: 32px;
      margin: 0 !important;
      background: rgba(var(--v-theme-primary), .1) !important;
      box-shadow: none !important;
      color: rgb(var(--v-theme-primary)) !important;
      font-size: .8rem;

      &:hover {
        background: rgba(var(--v-theme-primary), .16) !important;
      }
    }
  }

  // The preset list: a soft field without the underline.
  .v-field--variant-filled {
    border-radius: var(--cc-radius-sm);
    background: rgba(var(--v-theme-on-surface), .04);

    .v-field__overlay {
      opacity: 0;
    }

    .v-field__outline {
      display: none;
    }

    &.v-field--focused {
      box-shadow: 0 0 0 4px rgba(29, 78, 216, .15);
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

    .v-expansion-panel-title {
      min-height: 38px !important;
      padding: 0 10px !important;
      border-radius: var(--cc-radius);
      font-size: .72rem !important;
      text-transform: uppercase;
      letter-spacing: .06em;
      color: var(--cc-muted);

      b {
        font-weight: 700;
      }
    }

    .v-expansion-panel--active > .v-expansion-panel-title {
      color: rgb(var(--v-theme-primary));
    }

    .expansion-btn {
      margin-right: 6px;
      font-size: 18px;
    }

    .v-expansion-panel-text__wrapper {
      padding: 0 4px 4px !important;
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
