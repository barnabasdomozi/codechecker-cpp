<template>
  <v-dialog
    v-model="dialog"
    max-width="880px"
    scrollable
  >
    <template v-slot:activator="{ props: activatorProps }">
      <v-btn
        v-bind="activatorProps"
        id="report-info-btn"
        color="primary"
        variant="outlined"
        size="small"
        prepend-icon="mdi-information-outline"
      >
        Report Info
      </v-btn>
    </template>

    <v-card v-if="value" class="cc-ri">
      <header class="cc-ri__header">
        <span class="cc-ri__header-icon">
          <v-icon icon="mdi-information-outline" size="20" />
        </span>
        <div class="cc-ri__header-text">
          <div class="cc-ri__eyebrow">
            Report info
          </div>
          <div class="cc-ri__title" :title="value.checkerMsg">
            {{ value.checkerMsg }}
          </div>
        </div>
        <v-btn
          class="cc-ri__close"
          icon="mdi-close"
          size="small"
          variant="text"
          title="Close"
          @click="dialog = false"
        />
      </header>

      <div class="cc-ri__body">
        <!-- Statuses -->
        <div class="cc-ri__statuses">
          <router-link
            v-for="(status, idx) in statuses"
            :key="status.label"
            :to="status.to"
            class="cc-ri__status cc-rise"
            :style="{ '--cc-delay': `calc(${idx} * var(--cc-motion-step))` }"
            :title="`Show the reports with this ${status.label.toLowerCase()}`"
          >
            <span class="cc-ri__status-icon">
              <SeverityIcon
                v-if="status.kind === 'severity'"
                :status="value.severity"
                :size="22"
              />
              <DetectionStatusIcon
                v-else-if="status.kind === 'detection'"
                :status="value.detectionStatus"
                :size="22"
              />
              <ReviewStatusIcon
                v-else-if="status.kind === 'review'"
                :status="value.reviewData.status"
                size="22"
              />
              <span
                v-else
                class="cc-ri__path-length"
                :style="{
                  background: gradient.getGradientColor(bugPathLength)
                }"
              >
                {{ bugPathLength }}
              </span>
            </span>
            <span class="cc-ri__status-text">
              <span class="cc-ri__status-label">{{ status.label }}</span>
              <span class="cc-ri__status-value">{{ status.value }}</span>
            </span>
          </router-link>
        </div>

        <!-- Details -->
        <div class="cc-ri__grid">
          <section
            v-for="(section, sIdx) in sections"
            :key="section.title"
            class="cc-ri__card cc-rise"
            :class="{ 'cc-ri__card--wide': section.wide }"
            :style="{
              '--cc-delay': `calc(${sIdx + 2} * var(--cc-motion-step))`
            }"
          >
            <h3 class="cc-ri__card-title">
              <v-icon :icon="section.icon" size="16" />
              {{ section.title }}
            </h3>

            <dl class="cc-ri__rows">
              <div
                v-for="row in section.rows"
                :key="row.label"
                class="cc-ri__row"
                :class="{ 'cc-ri__row--full': row.full }"
              >
                <dt class="cc-ri__label">
                  {{ row.label }}
                </dt>
                <dd
                  class="cc-ri__value"
                  :class="{
                    'cc-ri__value--mono': row.mono,
                    'cc-ri__value--empty': row.value == null || row.value === ''
                  }"
                >
                  <template v-if="row.value == null || row.value === ''">
                    {{ row.empty ?? "—" }}
                  </template>
                  <router-link
                    v-else-if="row.to"
                    :to="row.to"
                    :title="row.title ?? `Show the reports of this ${
                      row.label.toLowerCase()}`"
                  >
                    {{ row.value }}
                  </router-link>
                  <span v-else :title="row.title">{{ row.value }}</span>

                  <a
                    v-if="row.doc"
                    class="cc-ri__doc"
                    :href="row.doc"
                    target="_blank"
                    rel="noopener"
                  >
                    Documentation
                    <v-icon icon="mdi-open-in-new" size="12" />
                  </a>
                </dd>
                <v-btn
                  v-if="row.copy && row.value != null && row.value !== ''"
                  class="cc-ri__copy"
                  size="x-small"
                  variant="text"
                  :icon="copied === row.label
                    ? 'mdi-check' : 'mdi-content-copy'"
                  :color="copied === row.label ? 'success' : undefined"
                  :title="copied === row.label
                    ? 'Copied' : `Copy ${row.label.toLowerCase()}`"
                  @click="copy(String(row.copyValue ?? row.value), row.label)"
                />
              </div>
            </dl>
          </section>
        </div>
      </div>
    </v-card>
  </v-dialog>
</template>

<script setup>
// Web UI v2: replaces
// src/components/Report/ReportInfo/ShowReportInfoDialog.vue (see
// vite.config.mjs). The details of the report on the report page: its
// statuses, then the checker, the location, the run and the history of the
// report as rows of labels and values. Values link to the report list
// filtered by them, as in the original (src/.../ReportInfo.vue).

import { computed, ref, watch } from "vue";

import {
  DetectionStatusIcon,
  ReviewStatusIcon,
  SeverityIcon
} from "@/components/Icons";
import { useDetectionStatus } from "@/composables/useDetectionStatus";
import { useGradientColor } from "@/composables/useGradientColor";
import { useReviewStatus } from "@/composables/useReviewStatus";
import { useSeverity } from "@/composables/useSeverity";
import { ccService, handleThriftError } from "@cc-api";
import { Checker, ReviewStatus, RunFilter } from "@cc/report-server-types";

const props = defineProps({
  value: { type: Object, default: null }
});

const dialog = ref(false);
const runName = ref(null);
const docUrl = ref(null);
const copied = ref(null);

const detectionStatus = useDetectionStatus();
const reviewStatus = useReviewStatus();
const severity = useSeverity();
const gradient = useGradientColor();

watch(dialog, open => {
  if (open) load();
});

watch(() => props.value, () => {
  if (dialog.value) load();
});

function load() {
  if (!props.value) return;
  runName.value = null;
  docUrl.value = null;
  fetchRunName();
  fetchDocUrl();
}

function fetchRunName() {
  const runFilter = new RunFilter({ ids: [ props.value.runId ] });
  ccService.getClient().getRunData(runFilter, 1).then(runs => {
    runName.value = runs[0]?.name ?? null;
  }).catch(() => {});
}

function fetchDocUrl() {
  const checker = new Checker({
    analyzerName: props.value.analyzerName,
    checkerId: props.value.checkerId
  });

  ccService.getClient().getCheckerLabels(
    [ checker ],
    handleThriftError(labels => {
      const docUrlLabel = labels[0].find(l => l.startsWith("doc_url"));
      docUrl.value = docUrlLabel ? docUrlLabel.split("doc_url:")[1] : null;
    })
  );
}

const toNumber = v => typeof v?.toNumber === "function" ? v.toNumber() : v;

// The report page loads the report with getReport(), which has no bug path
// length; reports opened from the report tree have one.
const bugPathLength = computed(() => {
  const length = toNumber(props.value?.bugPathLength);
  return length > 0 ? length : null;
});

const links = computed(() => {
  const v = props.value;
  return {
    runName: { name: "runs", query: { run: runName.value } },
    bugHash: { name: "reports", query: { "report-hash": v.bugHash } },
    checkedFile: { name: "reports", query: { filepath: v.checkedFile } },
    analyzerName: {
      name: "reports", query: { "analyzer-name": v.analyzerName } },
    checkerId: { name: "reports", query: { "checker-name": v.checkerId } },
    checkerMsg: { name: "reports", query: { "checker-msg": v.checkerMsg } },
    severity: { name: "reports", query: {
      severity: severity.severityFromCodeToString(v.severity) } },
    detectionStatus: { name: "reports", query: {
      "detection-status":
        detectionStatus.detectionStatusFromCodeToString(v.detectionStatus) } },
    reviewData: { name: "reports", query: {
      "review-status":
        reviewStatus.reviewStatusFromCodeToString(v.reviewData.status) } },
    bugPathLength: { name: "reports", query: {
      "min-bug-path-length": bugPathLength.value,
      "max-bug-path-length": bugPathLength.value } }
  };
});

const statuses = computed(() => {
  const v = props.value;
  const list = [ {
    kind: "severity",
    label: "Severity",
    value: severity.severityFromCodeToString(v.severity),
    to: links.value.severity
  }, {
    kind: "detection",
    label: "Detection status",
    value: detectionStatus.detectionStatusFromCodeToString(v.detectionStatus),
    to: links.value.detectionStatus
  }, {
    kind: "review",
    label: "Review status",
    value: reviewStatus.reviewStatusFromCodeToString(v.reviewData.status),
    to: links.value.reviewData
  } ];

  if (bugPathLength.value) {
    list.push({
      kind: "path",
      label: "Bug path length",
      value: bugPathLength.value === 1
        ? "1 step" : `${bugPathLength.value} steps`,
      to: links.value.bugPathLength
    });
  }

  return list;
});

// "2026-11-08 23:05:17.683145" -> "2026-11-08 23:05:17"
function formatDate(date) {
  return date ? String(date).split(".")[0] : null;
}

const sections = computed(() => {
  const v = props.value;
  const review = v.reviewData;
  const reviewed = review && review.status !== ReviewStatus.UNREVIEWED;

  const history = [ {
    label: "Detected",
    value: formatDate(v.detectedAt),
    title: v.detectedAt
  }, {
    label: "Fixed",
    value: formatDate(v.fixedAt),
    title: v.fixedAt,
    empty: "Not fixed"
  } ];

  if (reviewed) {
    history.push({
      label: "Reviewed by",
      value: [ review.author, formatDate(review.date) ]
        .filter(Boolean).join(" · "),
      title: review.isInSource
        ? "Set by a comment in the source code" : undefined
    });
    if (review.isInSource) {
      history.push({ label: "Source", value: "Source code comment" });
    }
    if (review.comment) {
      history.push({
        label: "Comment",
        value: review.comment,
        full: true,
        copy: true
      });
    }
  }

  const list = [ {
    title: "Checker",
    icon: "mdi-shield-bug-outline",
    rows: [ {
      label: "Analyzer",
      value: v.analyzerName,
      to: links.value.analyzerName
    }, {
      label: "Checker",
      value: v.checkerId,
      to: links.value.checkerId,
      mono: true,
      copy: true,
      doc: docUrl.value
    }, {
      label: "Message",
      value: v.checkerMsg,
      to: links.value.checkerMsg,
      copy: true
    } ]
  }, {
    title: "Run",
    icon: "mdi-play-box-multiple-outline",
    rows: [ {
      label: "Run",
      value: runName.value,
      to: links.value.runName,
      title: "Show the run",
      empty: "Loading…"
    }, {
      label: "Report ID",
      value: v.reportId != null ? String(v.reportId) : null,
      copy: true
    }, {
      label: "Report hash",
      value: v.bugHash,
      to: links.value.bugHash,
      title: "Show the reports with this hash",
      mono: true,
      copy: true
    } ]
  }, {
    title: "Location",
    wide: true,
    icon: "mdi-file-code-outline",
    rows: [ {
      label: "File",
      value: v.checkedFile,
      full: true,
      to: links.value.checkedFile,
      mono: true,
      copy: true
    }, {
      label: "Position",
      value: `Line ${v.line}, column ${v.column}`,
      copyValue: `${v.checkedFile}:${v.line}:${v.column}`,
      copy: true
    } ]
  }, {
    title: "History",
    wide: true,
    icon: "mdi-history",
    rows: history
  } ];

  const annotations = Object.entries(v.annotations ?? {});
  if (annotations.length) {
    list.push({
      title: "Annotations",
      icon: "mdi-tag-outline",
      wide: true,
      rows: annotations.map(([ key, value ]) => ({
        label: key, value, copy: true
      }))
    });
  }

  return list;
});

async function copy(text, key) {
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    // The Clipboard API needs HTTPS or localhost.
    const area = document.createElement("textarea");
    area.value = text;
    area.style.position = "fixed";
    area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    document.execCommand("copy");
    area.remove();
  }
  copied.value = key;
  setTimeout(() => {
    if (copied.value === key) copied.value = null;
  }, 1200);
}
</script>

<style lang="scss" scoped>
.cc-ri {
  display: flex;
  flex-direction: column;
  max-height: calc(100vh - 48px);
}

// Header: the checker message is the title.
.cc-ri__header {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  gap: 12px;
  padding: 16px 16px 14px 20px;
  border-bottom: 1px solid var(--cc-border);
}

.cc-ri__header-icon {
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

.cc-ri__header-text {
  flex: 1 1 auto;
  min-width: 0;
}

.cc-ri__eyebrow {
  color: var(--cc-muted);
  font-size: .75rem;
  font-weight: 600;
}

.cc-ri__title {
  overflow: hidden;
  font-size: 1.05rem;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.01em;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.cc-ri__close {
  color: var(--cc-muted);
}

.cc-ri__body {
  flex: 1 1 auto;
  min-height: 0;
  overflow-y: auto;
  padding: 20px;
}

// Statuses.
.cc-ri__statuses {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: 10px;
}

.cc-ri__status {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
  padding: 10px 12px;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius);
  background: rgba(var(--v-theme-on-surface), .02);
  color: inherit !important;
  text-decoration: none !important;
  transition: border-color .2s ease, background-color .2s ease;

  &:hover {
    border-color: rgba(var(--v-theme-primary), .35);
    background: rgba(var(--v-theme-primary), .05);
  }

  &:focus-visible {
    outline: none;
    box-shadow: 0 0 0 3px rgba(var(--v-theme-primary), .35);
  }
}

.cc-ri__status-icon {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: 10px;
  background: rgba(var(--v-theme-on-surface), .05);

  :deep(.v-avatar) {
    border-radius: 6px !important;
    font-size: .75rem;
  }
}

.cc-ri__path-length {
  display: flex;
  align-items: center;
  justify-content: center;
  min-width: 22px;
  height: 22px;
  padding: 0 5px;
  border-radius: 6px;
  color: #111827;
  font-size: .75rem;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}

.cc-ri__status-text {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.cc-ri__status-label {
  color: var(--cc-muted);
  font-size: .72rem;
}

.cc-ri__status-value {
  overflow: hidden;
  font-size: .9rem;
  font-weight: 650;
  white-space: nowrap;
  text-overflow: ellipsis;
}

// Detail cards.
.cc-ri__grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
  gap: 12px;
  margin-top: 16px;
}

.cc-ri__card {
  min-width: 0;
  padding: 12px 6px 6px;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius);

  &--wide {
    grid-column: 1 / -1;
  }
}

.cc-ri__card-title {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0 8px 6px;
  color: var(--cc-muted);
  font-size: .72rem;
  font-weight: 650;
  text-transform: uppercase;
  letter-spacing: .06em;
}

.cc-ri__rows {
  margin: 0;

  // A wide card puts its rows side by side.
  .cc-ri__card--wide > & {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    column-gap: 8px;
  }
}

.cc-ri__row {
  display: grid;
  grid-template-columns: 92px minmax(0, 1fr) 24px;
  align-items: start;
  gap: 8px;
  padding: 6px 4px 6px 8px;
  border-radius: var(--cc-radius-sm);
  transition: background-color .15s ease;

  &:hover {
    background: rgba(var(--v-theme-on-surface), .03);
  }

  &--full {
    grid-column: 1 / -1;
  }
}

.cc-ri__label {
  padding-top: 1px;
  color: var(--cc-muted);
  font-size: .78rem;
}

.cc-ri__value {
  min-width: 0;
  margin: 0;
  font-size: .85rem;
  font-weight: 550;
  line-height: 1.45;
  overflow-wrap: anywhere;

  a:not(.cc-ri__doc) {
    color: rgb(var(--v-theme-primary));
    text-decoration: none;

    &:hover {
      text-decoration: underline;
      text-underline-offset: 3px;
    }
  }

  &--mono {
    font-family: var(--cc-font-mono);
    font-size: .8rem;
    font-weight: 500;
  }

  &--empty {
    color: var(--cc-muted);
    font-family: var(--cc-font);
    font-weight: 450;
  }
}

.cc-ri__doc {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  margin-left: 8px;
  padding: 0 7px;
  border-radius: 999px;
  background: rgba(var(--v-theme-primary), .1);
  color: rgb(var(--v-theme-primary)) !important;
  font-family: var(--cc-font);
  font-size: .7rem;
  font-weight: 650;
  line-height: 18px;
  text-decoration: none !important;
  vertical-align: 1px;
  white-space: nowrap;

  &:hover {
    background: rgba(var(--v-theme-primary), .18);
  }
}

// The copy button of a row shows on hover.
.cc-ri__copy {
  width: 24px !important;
  height: 24px !important;
  margin-top: -2px;
  color: var(--cc-muted);
  opacity: 0;
  transition: opacity .15s ease;

  .cc-ri__row:hover &,
  &:focus-visible,
  &.text-success {
    opacity: 1;
  }

  @media (hover: none) {
    opacity: 1;
  }
}
</style>
