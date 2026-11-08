<template>
  <span
    :data-id="item.id"
    class="cc-tree-label"
    :class="`cc-tree-label--${kindName}`"
    :title="title"
  >
    <template v-if="item.kind === ReportTreeKind.REPORT">
      <span class="cc-tree-label__line">L{{ item.report.line }}</span>
      <span class="cc-tree-label__text">{{ item.report.checkerId }}</span>
      <span class="cc-tree-label__badge" title="Bug path length">
        {{ item.report.bugPathLength }}
      </span>
    </template>

    <template v-else-if="item.kind === ReportTreeKind.REPORT_STEPS">
      <span class="cc-tree-label__line">{{ stepLocation }}</span>
      <span class="cc-tree-label__text">{{ item.step.msg }}</span>
    </template>

    <template v-else-if="isExtendedReportDataItem">
      <span class="cc-tree-label__line">L{{ item.data.startLine }}</span>
      <span class="cc-tree-label__text">{{ item.name }}</span>
    </template>

    <template v-else-if="item.kind === ReportTreeKind.SEVERITY_LEVEL">
      <span class="cc-tree-label__text">{{ item.name }}</span>
      <span
        v-if="newReportCount"
        class="cc-tree-label__new"
        title="New reports"
      >
        {{ newReportCount }} new
      </span>
      <span class="cc-tree-label__count" title="Reports">
        {{ item.children.length }}
      </span>
    </template>

    <span v-else class="cc-tree-label__text">{{ item.name }}</span>
  </span>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportTree/ReportTreeLabel.vue
// (see vite.config.mjs). The label of a row of the report tree:
// - a report: its line, its checker and its bug path length;
// - a bug path step, macro expansion or note: its line and message;
// - a severity: its name, the number of new reports and all reports;
// - Outstanding / Closed: a section title.
// The full text is the tooltip, as long labels are cut.

import { DetectionStatus } from "@cc/report-server-types";
import { computed } from "vue";

import ReportTreeKind from "@/components/Report/ReportTree/ReportTreeKind";

const props = defineProps({
  item: { type: Object, required: true }
});

const KIND_NAMES = {
  [ReportTreeKind.SEVERITY_LEVEL]: "severity",
  [ReportTreeKind.DETECTION_STATUS]: "severity",
  [ReportTreeKind.REPORT]: "report",
  [ReportTreeKind.REPORT_STEPS]: "step",
  [ReportTreeKind.BUG]: "bug",
  [ReportTreeKind.MACRO_EXPANSION]: "group",
  [ReportTreeKind.MACRO_EXPANSION_ITEM]: "step",
  [ReportTreeKind.NOTE]: "group",
  [ReportTreeKind.NOTE_ITEM]: "step"
};

// Outstanding and Closed have no kind.
const kindName = computed(() => KIND_NAMES[props.item.kind] ?? "root");

const isExtendedReportDataItem = computed(() =>
  props.item.kind === ReportTreeKind.MACRO_EXPANSION_ITEM ||
  props.item.kind === ReportTreeKind.NOTE_ITEM
);

// "L12", or "file.c:12" for a step in another file.
const stepLocation = computed(() => {
  const { fileName, step } = props.item;
  return fileName ? `${fileName}:${step.startLine}` : `L${step.startLine}`;
});

const newReportCount = computed(() => {
  if (props.item.kind !== ReportTreeKind.SEVERITY_LEVEL) return 0;
  return props.item.children.filter(child =>
    child.report.detectionStatus === DetectionStatus.NEW
  ).length;
});

const title = computed(() => {
  const item = props.item;
  switch (item.kind) {
  case ReportTreeKind.REPORT:
    return `Line ${item.report.line}: ${item.report.checkerId} ` +
      `(bug path length: ${item.report.bugPathLength})`;
  case ReportTreeKind.REPORT_STEPS:
    return `${stepLocation.value}: ${item.step.msg}`;
  case ReportTreeKind.MACRO_EXPANSION_ITEM:
  case ReportTreeKind.NOTE_ITEM:
    return `Line ${item.data.startLine}: ${item.name}`;
  default:
    return item.name;
  }
});
</script>

<style lang="scss" scoped>
// The tree sets an inline display and the highlight color of macro and note
// rows on this element.
.cc-tree-label {
  display: flex !important;
  align-items: center;
  gap: 8px;
  min-width: 0;
  font-size: .8rem;
  line-height: 1.4;
}

.cc-tree-label__text {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.cc-tree-label__line {
  flex: 0 0 auto;
  color: var(--cc-muted);
  font-family: var(--cc-font-mono);
  font-size: .72rem;
  font-variant-numeric: tabular-nums;
}

.cc-tree-label__badge,
.cc-tree-label__count {
  flex: 0 0 auto;
  min-width: 20px;
  padding: 0 6px;
  border-radius: 999px;
  background: rgba(var(--v-theme-on-surface), .06);
  color: var(--cc-muted);
  font-size: .7rem;
  font-weight: 600;
  line-height: 18px;
  text-align: center;
  font-variant-numeric: tabular-nums;
}

.cc-tree-label__new {
  flex: 0 0 auto;
  padding: 0 6px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--color-soft-red) 14%, transparent);
  color: var(--color-soft-red);
  font-size: .7rem;
  font-weight: 650;
  line-height: 18px;
}

.cc-tree-label--root {
  color: var(--cc-muted);
  font-size: .72rem;
  font-weight: 650;
  text-transform: uppercase;
  letter-spacing: .06em;
}

.cc-tree-label--severity {
  font-weight: 600;
}

.cc-tree-label--bug {
  font-weight: 650;
}

.cc-tree-label--group {
  font-weight: 600;
}

// Macro and note rows keep their highlight color, as a soft pill.
.cc-tree-label--step[style*="background-color"] {
  margin-left: -4px;
  padding: 0 4px;
  border-radius: 4px;
}
</style>
