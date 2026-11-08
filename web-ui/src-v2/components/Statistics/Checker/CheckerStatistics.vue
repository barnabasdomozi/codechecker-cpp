<template>
  <v-container fluid class="cc-stats">
    <statistics-summary :tiles="summaryTiles" :loading="loading" />

    <statistics-panel
      icon="mdi-card-account-details-outline"
      title="Checker statistics"
      subtitle="Reports of each checker by review status. Click a number to
        list its reports."
      :delay="2"
      flush
    >
      <template #actions>
        <statistics-actions
          :loading="loading"
          @export="downloadCSV"
          @refresh="fetchStatistics"
        />
      </template>

      <checker-statistics-table
        :items="baseStats.statistics.value"
        :loading="loading"
      />
    </statistics-panel>

    <unique-stat-warning
      v-if="baseStats.reportFilter.value.isUnique"
    />
  </v-container>
</template>

<script setup>
// Web UI v2: replaces CheckerStatistics.vue of src/
// (see vite.config.mjs). The data is the original; the page has summary
// tiles and panels.
import { computed, ref } from "vue";

import { UniqueStatWarning } from "@/components/Statistics";
import {
  getCheckerStatistics
} from "@/components/Statistics/StatisticsHelper";
import { useBaseStatistics } from "@/composables/useBaseStatistics";
import { useSeverity } from "@/composables/useSeverity";
import { useToCSV } from "@/composables/useToCSV";
import CheckerStatisticsTable
  from "@/components/Statistics/Checker/CheckerStatisticsTable";
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
const baseStats = useBaseStatistics(props, getCheckerStatistics);

const loading = ref(false);

baseStats.setupRefreshListener(fetchStatistics);

// The key numbers above the table.
const summaryTiles = computed(() => {
  const items = baseStats.statistics.value;
  const totals = reviewStatusTotals(items);
  return [
    {
      label: "Checkers with reports",
      value: items.length,
      icon: "mdi-shield-bug-outline",
      tone: TONES.neutral
    },
    {
      label: "Outstanding reports",
      value: totals.outstanding,
      icon: "mdi-alert-circle-outline",
      tone: TONES.outstanding
    },
    {
      label: "Suppressed reports",
      value: totals.suppressed,
      icon: "mdi-eye-off-outline",
      tone: TONES.suppressed
    },
    {
      label: "All reports",
      value: totals.reports,
      icon: "mdi-bug-outline",
      tone: TONES.neutral
    }
  ];
});


function downloadCSV() {
  const _data = [
    [
      "Checker", "Severity", "Unreviewed",
      "Confirmed bug", "Outstanding reports (Unreviewed + Confirmed)",
      "False positive", "Intentional",
      "Suppressed reports (False positive + Intentional)", "All reports"
    ],
    ...baseStats.statistics.value.map(_stat => {
      return [
        _stat.checker, severity.severityFromCodeToString(_stat.severity),
        _stat.unreviewed.count, _stat.confirmed.count,
        _stat.outstanding.count, _stat.falsePositive.count,
        _stat.intentional.count, _stat.suppressed.count, _stat.reports.count,
      ];
    })
  ];

  csv.toCSV(_data, "codechecker_checker_statistics.csv");
}

async function fetchStatistics() {
  loading.value = true;

  const {
    runIds: _runIds,
    reportFilter: _reportFilter,
    cmpData: _cmpData
  } = baseStats.getStatisticsFilters();
  baseStats.statistics.value =
    await getCheckerStatistics(_runIds, _reportFilter, _cmpData);

  await baseStats.fetchDifference("checker");

  loading.value = false;
}
</script>
