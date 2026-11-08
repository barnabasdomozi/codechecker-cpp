<template>
  <div class="cc-report-tree">
    <header v-if="file" class="cc-report-tree__header" :title="file.path">
      <span class="cc-report-tree__icon">
        <v-icon icon="mdi-file-code-outline" size="20" />
      </span>
      <div class="cc-report-tree__titles">
        <div class="cc-report-tree__file">
          {{ file.name }}
        </div>
        <div class="cc-report-tree__dir">
          {{ file.dir }}
        </div>
      </div>
    </header>

    <OriginalReportTree
      :report="report"
      :review-status="reviewStatus"
      @click="emit('click', $event)"
    />
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportTree/ReportTree.vue (see
// vite.config.mjs). The original tree of the reports in the report's file,
// under a header with the file's name and folder that stays at the top
// while the tree scrolls. The rows are restyled here; their labels are
// src-v2/.../ReportTree/ReportTreeLabel.vue.

import { computed } from "vue";

import OriginalReportTree
  from "@v1/components/Report/ReportTree/ReportTree";

const props = defineProps({
  report: { type: Object, default: null },
  reviewStatus: { type: Number, default: null }
});

const emit = defineEmits([ "click" ]);

const file = computed(() => {
  const path = props.report?.checkedFile;
  if (!path) return null;

  const slash = path.lastIndexOf("/");
  // The folder is shown right to left, to cut it at the start (see the
  // styles). A left-to-right mark keeps its leading "/" in place.
  return {
    path,
    name: path.slice(slash + 1),
    dir: "‎" + (slash > 0 ? path.slice(0, slash) : "/")
  };
});
</script>

<style lang="scss" scoped>
.cc-report-tree {
  position: relative;
}

.cc-report-tree__header {
  position: sticky;
  top: 0;
  z-index: 2;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 14px;
  border-top: 1px solid var(--cc-border);
  border-bottom: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
}

.cc-report-tree__icon {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: rgba(var(--v-theme-primary), .12);
  color: rgb(var(--v-theme-primary));
}

.cc-report-tree__titles {
  min-width: 0;
}

.cc-report-tree__file {
  overflow: hidden;
  font-size: 1rem;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.01em;
  white-space: nowrap;
  text-overflow: ellipsis;
}

// The folder, cut at the start: its end is the part that tells files apart.
.cc-report-tree__dir {
  overflow: hidden;
  color: var(--cc-muted);
  font-size: .75rem;
  white-space: nowrap;
  text-overflow: ellipsis;
  direction: rtl;
  text-align: left;
}

// The tree.
.cc-report-tree :deep(.v-treeview) {
  padding: 8px 6px 12px;
  background: transparent;

  .v-list-item {
    min-height: 30px !important;
    margin: 1px 0;
    padding-inline-end: 6px !important;
    border-radius: var(--cc-radius-sm);
    transition: background-color .15s ease;

    &:hover > .v-list-item__overlay {
      opacity: .05;
    }
  }

  // The row of the shown report or step.
  .v-list-item--active {
    background: rgba(var(--v-theme-primary), .1);
    color: rgb(var(--v-theme-primary));

    > .v-list-item__overlay {
      opacity: 0 !important;
    }

    &::before {
      content: "";
      position: absolute;
      top: 7px;
      bottom: 7px;
      left: 0;
      width: 3px;
      border-radius: 3px;
      background: rgb(var(--v-theme-primary));
    }
  }

  .v-list-item__prepend {
    gap: 2px;

    // The tree's own spacing for levels: the indent below is enough.
    > .v-treeview-item__level {
      display: none !important;
    }

    > .v-list-item-action .v-btn {
      width: 24px;
      height: 24px;
      color: var(--cc-muted);
    }

    .v-icon {
      font-size: 18px !important;
    }

    .v-avatar {
      width: 18px !important;
      height: 18px !important;
      border-radius: 5px !important;
      font-size: .62rem;
    }
  }

  .v-list-item__spacer {
    width: 6px !important;
  }

  .v-list-item-title {
    overflow: visible;
  }

  // Each level a little further in.
  .v-list-group__items {
    --indent-padding: 0px;

    > .v-list-item,
    > .v-list-group > .v-list-item {
      padding-inline-start: 14px !important;
    }

    .v-list-group__items > .v-list-item,
    .v-list-group__items > .v-list-group > .v-list-item {
      padding-inline-start: 28px !important;
    }

    .v-list-group__items .v-list-group__items > .v-list-item,
    .v-list-group__items .v-list-group__items > .v-list-group > .v-list-item {
      padding-inline-start: 42px !important;
    }
  }
}
</style>
