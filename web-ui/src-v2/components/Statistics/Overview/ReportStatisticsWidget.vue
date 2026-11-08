<template>
  <div class="cc-trends">
    <section
      v-for="(reportType, typeIndex) in reportTypes"
      :key="reportType.id"
      class="cc-trend"
      :style="{
        '--cc-tone': reportType.color,
        '--cc-delay': `calc(var(--cc-motion-step) * ${typeIndex * 1.5})`
      }"
    >
      <header class="cc-trend__header">
        <span class="cc-trend__icon">
          <v-icon :icon="reportType.icon" size="20" />
        </span>
        <div class="cc-trend__titles">
          <h3 class="cc-trend__title">
            {{ reportType.label }}
            <tooltip-help-icon color="grey" size="x-small">
              <div
                v-if="reportType.id === 'new'"
                class="mb-2"
              >
                <p class="mb-2">
                  Shows the number of new outstanding reports since the last
                  <i>x</i> days. Clicking on each item will display
                  the corresponding reports in a list.
                </p>
                <p class="mb-2">
                  Closed reports: No longer detected by the analyzer or
                  identified as <b>false positive</b> or <b>intentional</b>
                  findings.
                </p>
                <p>
                  <b>Note: Clicking on any item will reset the filters to
                    reflect the displayed figures.</b>
                </p>
              </div>
              <div
                v-else
                class="mb-2"
              >
                <p class="mb-2">
                  Shows the number of reports which were closed in the last
                  <i>x</i> days. Clicking on each item will display the
                  corresponding reports in a list.
                </p>
                <p class="mb-2">
                  Closed reports: No longer detected by the analyzer or
                  identified as <b>false positive</b> or <b>intentional</b>
                  findings.
                </p>
                <p>
                  <b>Note: Clicking on any item will reset the filters to
                    reflect the displayed figures.</b>
                </p>
              </div>
              <div>
                The following filters does not affect these values:
                <ul>
                  <li><b>Outstanding reports on a given date</b> filter.</li>
                  <li>All filters in the <b>COMPARE TO</b> section.</li>
                  <li><b>Latest Review Status</b> filter.</li>
                  <li><b>Latest Detection Status</b> filter.</li>
                </ul>
              </div>
            </tooltip-help-icon>
          </h3>
          <div class="cc-trend__subtitle">
            {{ reportType.subtitle }}
          </div>
        </div>
      </header>

      <div class="cc-trend__metrics">
        <router-link
          v-for="(columnData, colIndex) in reportType.cols"
          :key="columnData.label"
          :to="{
            name: 'reports',
            query: {
              ...{
                'detection-status': undefined,
                'run': runName,
              },
              ...(reportType.id === 'new' ? {
                'diff-type': 'New',
                'newcheck': runName,
                'open-reports-date':
                  dateUtils.dateTimeToStr(columnData.date[0]),
                'compared-to-open-reports-date':
                  dateUtils.dateTimeToStr(columnData.date[1]),
              } : {
                'fixed-after':
                  dateUtils.dateTimeToStr(columnData.date[0]),
                'fixed-before':
                  dateUtils.dateTimeToStr(columnData.date[1])
              })
            }
          }"
          class="cc-metric"
          :style="{
            '--cc-delay':
              `calc(var(--cc-motion-step) * ${typeIndex * 1.5 + colIndex})`
          }"
          :title="`Show these reports (${columnData.label.toLowerCase()})`"
        >
          <span class="cc-metric__label">{{ columnData.label }}</span>
          <span
            v-if="columnData.loading || columnData.value === null"
            class="cc-metric__placeholder"
          />
          <span
            v-else
            class="cc-metric__value"
            :class="{ 'cc-metric__value--zero': columnData.value === 0 }"
          >
            {{ columnData.value.toLocaleString() }}
          </span>
          <v-icon
            class="cc-metric__arrow"
            icon="mdi-arrow-top-right"
            size="16"
          />
        </router-link>
      </div>
    </section>
  </div>
</template>

<script setup>
// Web UI v2: replaces ReportStatisticsWidget.vue of src/ (see
// vite.config.mjs). The data and links are the original ones.

import {
  endOfToday,
  startOfToday,
  startOfYesterday,
  subDays
} from "date-fns";
import { ref } from "vue";
import { useRouter } from "vue-router";

import { useDateUtils } from "@/composables/useDateUtils";
import { ccService, handleThriftError } from "@cc-api";
import {
  CompareData,
  DateInterval,
  DiffType,
  ReportDate,
  ReportFilter
} from "@cc/report-server-types";

import TooltipHelpIcon from "@/components/TooltipHelpIcon.vue";

const props = defineProps({
  bus: { type: Object, required: true },
  runIds: {
    required: true,
    validator: _v => typeof _v === "object" || _v === null
  },
  reportFilter: { type: Object, required: true },
});

const router = useRouter();
const dateUtils = useDateUtils();

const last7Days = subDays(endOfToday(), 7);
const last31Days = subDays(endOfToday(), 31);
const runName = router.currentRoute.value.query["run"];

const cols = [
  { label: "Today",  date: [ startOfToday(), endOfToday() ] },
  { label: "Yesterday", date: [ startOfYesterday(), endOfToday() ] },
  { label: "Last 7 days", date: [ last7Days, endOfToday() ] },
  { label: "Last 31 days", date: [ last31Days, endOfToday() ] }
];

const reportTypes = ref([
  {
    id: "new",
    label: "New outstanding reports",
    subtitle: "Reports that became outstanding in each period",
    color: "var(--color-soft-red)",
    icon: "mdi-trending-up",
    getValue: getNewReports,
    cols: cols.map(_c => ({ ..._c, value: null, loading: null }))
  },
  {
    id: "resolved",
    label: "Resolved reports",
    subtitle: "Reports closed in each period",
    color: "var(--color-soft-green)",
    icon: "mdi-check-circle-outline",
    getValue: getResolvedReports,
    cols: cols.map(_c => ({ ..._c, value: null, loading: null }))
  },
]);

function fetchValues() {
  reportTypes.value.forEach(_type =>
    _type.cols.forEach(_column =>  _type.getValue(_column, _column.date)));
}

function getReportCount(column, runIds, reportFilter, cmpData) {
  column.loading = "white";

  ccService.getClient().getRunResultCount(runIds, reportFilter, cmpData,
    handleThriftError(_res => {
      column.value = _res.toNumber();
      column.loading = null;
    }));
}

function getNewReports(column, date) {
  const _rFilter = new ReportFilter(props.reportFilter);
  _rFilter.detectionStatus = null;
  _rFilter.reviewStatus = null;
  _rFilter.openReportsDate = dateUtils.getUnixTime(date[0]);

  const _cmpData = new CompareData({
    runIds: props.runIds,
    openReportsDate: dateUtils.getUnixTime(date[1]),
    diffType: DiffType.NEW
  });

  getReportCount(column, props.runIds, _rFilter, _cmpData);
}

function getResolvedReports(column, date) {
  const _rFilter = new ReportFilter(props.reportFilter);
  _rFilter.reviewStatus = null;
  _rFilter.detectionStatus = null;
  _rFilter.date = new ReportDate({
    fixed: new DateInterval({
      after: dateUtils.getUnixTime(date[0]),
      before: dateUtils.getUnixTime(date[1])
    })
  });

  const _cmpData = null;
  getReportCount(column, props.runIds, _rFilter, _cmpData);
}

defineExpose({ fetchValues });
</script>

<style lang="scss" scoped>
.cc-trends {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
  gap: 16px;
}

.cc-trend {
  padding: 18px 20px 20px;
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay);
}

.cc-trend__header {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  margin-bottom: 16px;
}

// A tinted tile in the panel's color.
.cc-trend__icon {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: color-mix(in srgb, var(--cc-tone) 12%, transparent);
  color: var(--cc-tone);
}

.cc-trend__title {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0;
  font-size: 1rem;
  font-weight: 650;
  letter-spacing: -.01em;
}

.cc-trend__subtitle {
  font-size: .8rem;
  color: var(--cc-muted);
}

.cc-trend__metrics {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 10px;
}

// One period: a link to its reports.
.cc-metric {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 12px 14px;
  border-radius: var(--cc-radius);
  border: 1px solid var(--cc-border);
  background: rgba(var(--v-theme-on-surface), .02);
  color: inherit !important;
  text-decoration: none !important;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay);
  transition: border-color .2s ease, background-color .2s ease,
    box-shadow .2s ease;

  &:hover {
    border-color: color-mix(in srgb, var(--cc-tone) 45%, transparent);
    background: color-mix(in srgb, var(--cc-tone) 5%, transparent);
    box-shadow: var(--cc-shadow-sm);

    .cc-metric__arrow {
      opacity: 1;
      transform: none;
    }
  }
}

// One line, so the numbers of all periods line up.
.cc-metric__label {
  overflow: hidden;
  font-size: .78rem;
  font-weight: 600;
  white-space: nowrap;
  text-overflow: ellipsis;
  color: var(--cc-muted);
}

.cc-metric__value {
  font-size: 1.75rem;
  font-weight: 700;
  line-height: 1.15;
  letter-spacing: -.02em;
  font-variant-numeric: tabular-nums;
  color: var(--cc-tone);
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;

  // A translucent color: the fade-in animation sets the opacity.
  &--zero {
    color: rgba(var(--v-theme-on-surface), .4);
  }
}

// Shimmers in place of the number while it loads.
.cc-metric__placeholder {
  position: relative;
  width: 56px;
  height: 32px;
  overflow: hidden;
  border-radius: 6px;
  background: rgba(var(--v-theme-on-surface), .07);

  &::after {
    content: "";
    position: absolute;
    inset: 0;
    background: linear-gradient(
      90deg, transparent, rgba(var(--v-theme-on-surface), .08), transparent
    );
    animation: cc-shimmer 1.2s ease-in-out infinite;
  }
}

// Shows on hover that the period links to its reports.
.cc-metric__arrow {
  position: absolute;
  right: 10px;
  bottom: 14px;
  color: var(--cc-tone);
  opacity: 0;
  transform: translate(-3px, 3px);
  transition: opacity .2s ease, transform .2s ease;
}

@media (max-width: 700px) {
  .cc-trends {
    grid-template-columns: 1fr;
  }

  .cc-trend__metrics {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
