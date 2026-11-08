<template>
  <div class="cc-report-detail">
    <OriginalReportDetail />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/views/ReportDetail.vue (see vite.config.mjs). It
// shows the original page; the sidebar beside the report is laid out as a
// card, like the filter sidebar of the report list: Back to reports at the
// top, then the file and its reports (src-v2/.../ReportTree/ReportTree.vue).

import OriginalReportDetail from "@v1/views/ReportDetail";
</script>

<style lang="scss" scoped>
// The sidebar pane.
.cc-report-detail :deep(.splitpanes__pane:first-child > .v-container) {
  margin-right: 4px;
  overflow: hidden;
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;

  > .v-row:first-child {
    padding: 10px 10px 8px;

    > .v-col {
      padding: 0 !important;
    }
  }
}

// Back to reports: a tinted primary button at the top of the card, like
// Clear all in the filter sidebar.
.cc-report-detail :deep(#back-to-reports-btn) {
  justify-content: flex-start;
  height: 36px;
  margin: 0 !important;
  padding: 0 12px;
  border: none;
  border-radius: var(--cc-radius-sm);
  background: rgba(var(--v-theme-primary), .1);
  color: rgb(var(--v-theme-primary)) !important;
  font-size: .85rem;
  font-weight: 600;
  transition: background-color .2s ease, color .2s ease;

  .v-btn__content {
    gap: 8px;
  }

  .v-icon {
    font-size: 18px;
    transition: transform .2s var(--cc-ease-out);
  }

  .v-btn__overlay,
  .v-btn__underlay {
    display: none;
  }

  &:hover {
    background: rgba(var(--v-theme-primary), .18);

    .v-icon {
      transform: translateX(-3px);
    }
  }
}
</style>
