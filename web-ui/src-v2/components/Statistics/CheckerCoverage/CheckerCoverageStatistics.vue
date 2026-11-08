<template>
  <v-container fluid class="cc-stats">
    <statistics-dialog
      v-if="type"
      v-model="showRuns[type]"
      :checker-name="selectedCheckerName"
      :type="type"
      :run-data="runData"
    />

    <statistics-summary :tiles="summaryTiles" :loading="loading" />

    <statistics-panel
      icon="mdi-clipboard-check-outline"
      title="Checker coverage"
      subtitle="The checkers enabled in the selected runs. Only the Run / Tag
        filter and Unique reports affect this page."
      :delay="2"
      flush
    >
      <template #help>
        The tab shows all enabled checkers in the selected runs.
        <br><br>
        Please, first select runs in the left "Run/Tag Filter" menu.
        If the filter is empty all runs are selected.
        Specifying a run tag is not applied.
        <br><br>
        Please, note that this feature is available only for runs analysed
        natively with <strong>CodeChecker 6.24</strong> and above.
      </template>

      <template #actions>
        <statistics-actions
          :loading="loading"
          @export="downloadCSV"
          @refresh="fetchStatistics"
        />
      </template>

      <checker-coverage-statistics-table
        :items="statistics"
        :loading="loading"
        @enabled-click="showingRuns"
      />
    </statistics-panel>
  </v-container>
</template>

<script setup>
// Web UI v2: replaces CheckerCoverageStatistics.vue of src/
// (see vite.config.mjs). The data is the original; the page has summary
// tiles and panels.
import { computed, ref, watch } from "vue";

import { useBaseStatistics } from "@/composables/useBaseStatistics";
import { useSeverity } from "@/composables/useSeverity";
import { useToCSV } from "@/composables/useToCSV";
import { ccService, handleThriftError } from "@cc-api";
import {
  Checker,
  ReportFilter,
  RunFilter
} from "@cc/report-server-types";
import StatisticsDialog from "@/components/Statistics/StatisticsDialog";
import CheckerCoverageStatisticsTable from
  "@/components/Statistics/CheckerCoverage/CheckerCoverageStatisticsTable";
import StatisticsActions
  from "@v2/components/Statistics/StatisticsActions";
import StatisticsPanel from "@v2/components/Statistics/StatisticsPanel";
import StatisticsSummary
  from "@v2/components/Statistics/StatisticsSummary";
import { TONES } from "@v2/components/Statistics/summaryTotals";


const props = defineProps({
  bus: { type: Object, required: true }
});

const severity = useSeverity();
const csv = useToCSV();
const baseStats = useBaseStatistics(props, null);

// The key numbers above the table.
const summaryTiles = computed(() => {
  const items = statistics.value;
  const sum = field => items.reduce((total, item) => total + item[field], 0);
  return [
    {
      label: "Checkers",
      value: items.length,
      icon: "mdi-shield-check-outline",
      tone: TONES.neutral
    },
    {
      label: "Enabled in all runs",
      value: items.filter(item => item.enabledInAllRuns).length,
      icon: "mdi-check-all",
      tone: TONES.suppressed
    },
    {
      label: "Outstanding reports",
      value: sum("outstanding"),
      icon: "mdi-alert-circle-outline",
      tone: TONES.outstanding
    },
    {
      label: "Closed reports",
      value: sum("closed"),
      icon: "mdi-check-circle-outline",
      tone: TONES.suppressed
    }
  ];
});

const checker_stat = ref({});
const loading = ref(false);
const noProperRun = ref(false);
const runs = ref(null);
const runData = ref([]);
const selectedCheckerName = ref(null);
const showRuns = ref({
  enabled: false,
  disabled: false,
  unknown: false,
});
const statistics = ref([]);
const type = ref(null);

watch(checker_stat, function(_stat) {
  statistics.value = Object.keys(_stat).map(_checker_id => {
    return {
      checker: _stat[_checker_id].checkerName,
      severity: _stat[_checker_id].severity,
      guidelineRules: _stat[_checker_id].guidelineRules,
      enabledInAllRuns: _stat[_checker_id].disabled.length === 0
        ? 1
        : 0,
      enabledRunLength: _stat[_checker_id].enabled.length,
      disabledRunLength: _stat[_checker_id].disabled.length,
      unknownRunLength: _stat[_checker_id].unknown.length,
      closed: _stat[_checker_id].closed.toNumber(),
      outstanding: _stat[_checker_id].outstanding.toNumber(),
    };
  });
});

watch(function() { return baseStats.runIds.value; }, async function() {
  noProperRun.value = false;
});

baseStats.setupRefreshListener(fetchStatistics);

function downloadCSV() {
  const _data = [
    [
      "Checker Name", "guideline", "Severity", "Status",
      "Closed Reports", "Outstanding Reports",
    ],
    ...statistics.value.map(_stat => {
      return [
        _stat.checker,
        formattedGuidelines(_stat.guidelineRules),
        severity.severityFromCodeToString(_stat.severity),
        _stat.enabledInAllRuns
          ? "Enabled in all selected runs"
          : "Not enabled in all selected runs",
        _stat.closed,
        _stat.outstanding
      ];
    })
  ];

  csv.toCSV(_data, "codechecker_checker_coverage_statistics.csv");
}

async function fetchStatistics() {
  loading.value = true;

  const _run_filter = new RunFilter({
    ids: baseStats.runIds.value
  });

  runs.value = await ccService.getRuns(_run_filter);

  const _filter = new ReportFilter(baseStats.reportFilter.value);

  const _checker_stat = await new Promise(_resolve => {
    ccService.getClient().getCheckerStatusVerificationDetails(
      baseStats.runIds.value,
      _filter,
      handleThriftError(_res => {
        _resolve(_res);
      }));
  });

  if ( _checker_stat !== undefined ) {
    const _checkers = Object.values(_checker_stat).map(_stat => {
      return new Checker({
        analyzerName: _stat.analyzerName,
        checkerId: _stat.checkerName
      });
    });

    const _guidelineRules = await new Promise(_resolve => {
      ccService.getClient().getCheckerLabels(
        _checkers,
        handleThriftError(_labels => {
          _resolve(_labels.map(_label => {
            const _guidelines = _label.filter(
              _param => _param.startsWith("guideline")
            );
            return _guidelines.map(_g => {
              const _guideline = _g.split("guideline:")[1];
              const _guidelineLabels = _label.filter(
                _param => _param.startsWith(_guideline)
              );
              return {
                type: _guideline,
                rules: _guidelineLabels.map(_gl => {
                  return _gl.split(`${_guideline}:`)[1];
                })
              };
            });
          }));
        })
      );
    });

    Object.keys(_checker_stat).forEach(
      (_key, _index) => {
        _checker_stat[_key]["guidelineRules"] = _guidelineRules[_index]
        !== undefined
          ? _guidelineRules[_index]
          : null;
      });
  }

  checker_stat.value = _checker_stat;
  loading.value = false;
}

function showingRuns(_type, _checker_name) {
  type.value = _type;
  selectedCheckerName.value = _checker_name;
  const _checker_id = Object.keys(checker_stat.value).find(_checker_id =>
    checker_stat.value[_checker_id].checkerName === _checker_name
  );

  if (_checker_id) {
    runData.value = checker_stat.value[_checker_id][_type].map(
      run_id => runs.value.find(
        runData => runData.runId.toNumber() === run_id.toNumber()
      )
    );
  }
  else {
    runData.value = runs.value;
  }
  showRuns.value[_type] = true;
}

function formattedGuidelines(_guidelineRules) {
  return _guidelineRules.map(_guidelineRule => {
    const _rules = _guidelineRule.rules.join("; ");
    return `${_guidelineRule.type}: ${_rules}`;
  }).join("  ");
}
</script>
