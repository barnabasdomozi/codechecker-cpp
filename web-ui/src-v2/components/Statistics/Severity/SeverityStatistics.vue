<template>
  <v-container fluid class="cc-stats">
    <statistics-summary :tiles="summaryTiles" :loading="loading" />

    <statistics-panel
      icon="mdi-speedometer"
      title="Severity statistics"
      subtitle="Reports of each severity by review status. Click a number to
        list its reports."
      :delay="2"
      flush
    >
      <template #help>
        This table shows severity statistics for the product.
        <br><br>
        The following filters don't affect these values:
        <ul>
          <li><b>Severity</b> filter.</li>
          <li><b>Source component</b> filter.</li>
        </ul>
      </template>

      <template #actions>
        <statistics-actions
          :loading="loading"
          @export="downloadCSV"
          @refresh="fetchStatistics"
        />
      </template>

      <severity-statistics-table
        :items="statistics"
        :loading="loading"
      />
    </statistics-panel>

    <unique-stat-warning
      v-if="baseStats.reportFilter.value.isUnique"
    />

    <component-severity-statistics :bus="bus" />
  </v-container>
</template>

<script setup>
// Web UI v2: replaces SeverityStatistics.vue of src/
// (see vite.config.mjs). The data is the original; the page has summary
// tiles and panels.
import { computed, ref } from "vue";

import { useBaseStatistics } from "@/composables/useBaseStatistics";
import { useSeverity } from "@/composables/useSeverity";
import { useToCSV } from "@/composables/useToCSV";

import { UniqueStatWarning } from "@/components/Statistics";
import {
  getSeverityStatistics
} from "@/components/Statistics/StatisticsHelper";
import {
  ComponentSeverityStatistics
} from "@/components/Statistics/Severity/ComponentSeverityStatistics";

import SeverityStatisticsTable
  from "@/components/Statistics/Severity/SeverityStatisticsTable";
import StatisticsActions
  from "@v2/components/Statistics/StatisticsActions";
import StatisticsPanel from "@v2/components/Statistics/StatisticsPanel";
import StatisticsSummary
  from "@v2/components/Statistics/StatisticsSummary";
import {
  TONES,
  reviewStatusTotals
} from "@v2/components/Statistics/summaryTotals";


const props = defineProps({
  bus: { type: Object, required: true }
});

const severity = useSeverity();
const csv = useToCSV();
const baseStats = useBaseStatistics(props, getSeverityStatistics);

const loading = ref(false);
const statistics = ref([]);

baseStats.setupRefreshListener(fetchStatistics);

// The key numbers above the table: all outstanding reports, then the
// outstanding reports of each severity, highest first.
const summaryTiles = computed(() => {
  const totals = reviewStatusTotals(statistics.value);
  return [
    {
      label: "Outstanding reports",
      value: totals.outstanding,
      icon: "mdi-alert-circle-outline",
      tone: TONES.outstanding
    },
    ...[ ...statistics.value ]
      .sort((a, b) => b.severity - a.severity)
      .map(stat => ({
        label: severity.severityFromCodeToString(stat.severity),
        value: stat.outstanding.count,
        icon: "mdi-speedometer",
        tone: severity.severityFromCodeToColor(stat.severity)
      }))
  ];
});


function downloadCSV() {
  const _data = [
    [ "Severity", "Unreviewed", "Confirmed bug",
      "Outstanding reports (Unreviewed + Confirmed)", "False positive",
      "Intentional", "Suppressed reports (False positive + Intentional)",
      "All reports"
    ],
    ...statistics.value.map(_stat => {
      return [
        severity.severityFromCodeToString(_stat.severity),
        _stat.unreviewed.count, _stat.confirmed.count,
        _stat.outstanding.count, _stat.falsePositive.count,
        _stat.intentional.count, _stat.suppressed.count, _stat.reports.count
      ];
    })
  ];

  csv.toCSV(_data, "codechecker_severity_statistics.csv");
}

async function fetchStatistics() {
  loading.value = true;

  const {
    runIds: _runIds,
    reportFilter: _reportFilter,
    cmpData: _cmpData
  } = baseStats.getStatisticsFilters();
  statistics.value =
    await getSeverityStatistics(_runIds, _reportFilter, _cmpData);

  await baseStats.fetchDifference("severity");

  loading.value = false;
}
</script>

<style>
.severity {
  text-decoration: none;
}
</style>
