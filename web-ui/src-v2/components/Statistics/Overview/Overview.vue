<template>
  <v-container fluid class="cc-overview">
    <report-statistics-widget
      ref="reportsRef"
      :bus="bus"
      :run-ids="baseStats.runIds.value"
      :report-filter="baseStats.reportFilter.value"
    />

    <div class="cc-overview__kpis">
      <single-line-widget
        icon="mdi-file-alert-outline"
        color="var(--color-soft-red)"
        label="Failed files"
        :bus="bus"
        :value="failedFiles"
      >
        <template #help>
          Number of failed files in the current product.<br><br>
          Only the Run filter will affect this value.
        </template>

        <template #value="{ value }">
          <failed-files-dialog>
            <template #default="{ on }">
              <span
                v-bind="on"
                class="num-of-failed-files"
                title="Show the failed files"
              >
                {{ value.toLocaleString() }}
              </span>
            </template>
          </failed-files-dialog>
        </template>
      </single-line-widget>

      <single-line-widget
        icon="mdi-shield-bug-outline"
        color="rgb(var(--v-theme-primary))"
        label="Checkers reporting faults"
        :bus="bus"
        :value="activeCheckers"
      >
        <template #help>
          Number of checkers which found some report in the current
          product.<br><br>

          Every filter will affect this value.
        </template>
      </single-line-widget>
    </div>

    <v-card class="cc-chart" flat>
      <header class="cc-chart__header">
        <span class="cc-chart__icon">
          <v-icon icon="mdi-chart-timeline-variant" size="20" />
        </span>
        <div class="cc-chart__titles">
          <h3 class="cc-chart__title">
            Outstanding reports

            <tooltip-help-icon color="grey" size="x-small">
              Shows the number of reports which were active in the last
              <i>x</i> months/days.<br><br>

              The following filters don't affect these values:
              <ul>
                <li><b>Outstanding reports on a given date</b> filter.</li>
                <li>All filters in the <b>COMPARE TO</b> section.</li>
                <li><b>Latest Review Status</b> filter.</li>
                <li><b>Latest Detection Status</b> filter.</li>
              </ul>
            </tooltip-help-icon>
          </h3>
          <div class="cc-chart__subtitle">
            Reports open at the end of each {{ resolutionUnit }}, by severity.
            Click a point to list its reports.
          </div>
        </div>

        <v-spacer />

        <v-form ref="form" class="cc-chart__interval">
          <span class="cc-chart__interval-label">Last</span>
          <v-text-field
            :model-value="interval"
            class="interval"
            type="number"
            min="1"
            max="31"
            hide-details
            density="compact"
            variant="outlined"
            aria-label="Interval"
            @update:model-value="setInterval"
          />
          <v-select
            :model-value="resolution"
            class="resolution"
            :items="resolutions"
            hide-details
            density="compact"
            variant="outlined"
            aria-label="Resolution"
            @update:model-value="setResolution"
          />
        </v-form>
      </header>

      <div v-if="intervalError" class="cc-chart__error">
        {{ intervalError }}
      </div>

      <outstanding-reports-chart
        :bus="bus"
        :get-statistics-filters="baseStats.getStatisticsFilters"
        :interval="interval"
        :resolution="resolution"
        :styles="{
          height: '380px',
          position: 'relative'
        }"
      />
    </v-card>
  </v-container>
</template>

<script setup>
// Web UI v2: replaces Overview.vue of src/ (see vite.config.mjs). The data
// is the original; the page is laid out as a dashboard.

import _ from "lodash";
import { computed, ref } from "vue";
import { useRoute, useRouter } from "vue-router";

import TooltipHelpIcon from "@/components/TooltipHelpIcon";
import { useBaseStatistics } from "@/composables/useBaseStatistics";
import { ccService, handleThriftError } from "@cc-api";
import { MAX_QUERY_SIZE } from "@cc/report-server-types";

// Through src/, so the widgets that src-v2/ replaces are the v2 ones.
import FailedFilesDialog
  from "@/components/Statistics/Overview/FailedFilesDialog";
import OutstandingReportsChart
  from "@/components/Statistics/Overview/OutstandingReportsChart";
import ReportStatisticsWidget
  from "@/components/Statistics/Overview/ReportStatisticsWidget";
import SingleLineWidget
  from "@/components/Statistics/Overview/SingleLineWidget";

const props = defineProps({
  bus: { type: Object, required: true }
});

const route = useRoute();
const router = useRouter();
const baseStats = useBaseStatistics(props, null);
const failedFiles = ref(null);
const activeCheckers = ref(null);

const defaultInterval = "7";
const resolutions = [ "days", "weeks", "months", "years" ];
const defaultResolution = resolutions[0];

const loading = ref(false);
const reportsRef = ref(null);

baseStats.setupRefreshListener(fetchValues);

let _interval = route.query["interval"];
if (validateIntervalValue(_interval)) {
  _interval = defaultInterval;
}

let _resolution = route.query["resolution"];
if (!_resolution || !resolutions.includes(_resolution)) {
  _resolution = defaultResolution;
}

const intervalError = ref(null);
const interval = ref(_interval);
const resolution = ref(_resolution);

// "day", "week", ... for the chart's subtitle.
const resolutionUnit = computed(() => resolution.value.replace(/s$/, ""));

async function fetchValues() {
  loading.value = true;
  failedFiles.value = await getNumberOfFailedFiles();
  activeCheckers.value = await getNumberOfActiveCheckers();
  reportsRef.value?.fetchValues();
  loading.value = false;
}

function validateIntervalValue(val) {
  if (!val || isNaN(parseInt(val))) {
    return "Number is required!";
  }

  if (parseInt(val) > 31) {
    return "Interval value should between 1-31!";
  }

  return null;
}

const setInterval = _.debounce(function(_val) {
  intervalError.value = validateIntervalValue(_val);
  if (intervalError.value) return;

  interval.value = _val;
  updateUrl();

  intervalError.value = null;
}, 300);

function setResolution(_val) {
  resolution.value = _val;
  updateUrl();
}

function updateUrl() {
  const _queryParams = Object.assign({}, route.query, {
    interval: interval.value,
    resolution: resolution.value
  });

  router.replace({ query: _queryParams }).catch(() => {});
}

function getNumberOfFailedFiles() {
  return new Promise(_resolve => {
    ccService.getClient().getFailedFilesCount(
      baseStats.runIds.value,
      handleThriftError(_res => {
        _resolve(_res.toNumber());
      }));
  });
}

function getNumberOfActiveCheckers() {
  const {
    runIds: _runIds,
    reportFilter: _reportFilter,
    cmpData: _cmpData
  } = baseStats.getStatisticsFilters();
  const _limit = MAX_QUERY_SIZE;
  const _offset = 0;

  return new Promise(_resolve => {
    ccService.getClient().getCheckerCounts(_runIds, _reportFilter, _cmpData,
      _limit, _offset, handleThriftError(_res => {
        _resolve(_res.length);
      }));
  });
}
</script>

<style lang="scss" scoped>
.cc-overview {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 16px 20px 24px;
}

.cc-overview__kpis {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;

  > * {
    animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
    animation-delay: var(--cc-motion-step);
  }
}

.num-of-failed-files {
  cursor: pointer;
  border-bottom: 1px dashed currentColor;
}

.cc-chart {
  padding: 18px 20px 12px;
  border-radius: var(--cc-radius-lg) !important;
  border: 1px solid var(--cc-border) !important;
  box-shadow: var(--cc-shadow-sm) !important;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: calc(var(--cc-motion-step) * 3);
}

.cc-chart__header {
  display: flex;
  flex-wrap: wrap;
  align-items: flex-start;
  gap: 12px;
  margin-bottom: 12px;
}

.cc-chart__icon {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: rgba(var(--v-theme-primary), .1);
  color: rgb(var(--v-theme-primary));
}

.cc-chart__title {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0;
  font-size: 1rem;
  font-weight: 650;
  letter-spacing: -.01em;
}

.cc-chart__subtitle {
  font-size: .8rem;
  color: var(--cc-muted);
}

.cc-chart__interval {
  display: flex;
  align-items: center;
  gap: 8px;

  .interval {
    width: 84px;
  }

  .resolution {
    width: 124px;
  }
}

.cc-chart__interval-label {
  font-size: .85rem;
  font-weight: 600;
  color: var(--cc-muted);
}

.cc-chart__error {
  margin: -4px 0 8px;
  font-size: .8rem;
  color: rgb(var(--v-theme-error));
  text-align: right;
}
</style>
