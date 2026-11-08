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
      icon="mdi-clipboard-text-outline"
      title="Guideline statistics"
      subtitle="The rules of a coding guideline and the checkers that cover
        them in the selected runs."
      :delay="2"
      flush
    >
      <template #help>
        The tab displays the checker statistics for all the rules of the
        selected guideline, which are associated with the specified runs.
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

      <template #toolbar>
        <v-select
          v-model="selectedGuidelineIndex"
          class="cc-stats__guideline"
          :items="guidelineOptions"
          item-title="name"
          item-value="value"
          label="Guideline"
          prepend-inner-icon="mdi-book-open-variant"
          variant="outlined"
          density="compact"
          hide-details
        >
          <template v-slot:selection="{ item }">
            <div class="selection-item">
              {{ item.raw.name }}
            </div>
          </template>
        </v-select>
        <v-switch
          v-model="hideNotOutstanding"
          label="Hide compliant rules"
          color="primary"
          density="compact"
          hide-details
          inset
        />
      </template>

      <guideline-statistics-table
        :items="filteredStatistics"
        :loading="loading"
        @enabled-click="showingRuns"
      />
    </statistics-panel>
  </v-container>
</template>

<script setup>
// Web UI v2: replaces GuidelineStatistics.vue of src/
// (see vite.config.mjs). The data is the original; the page has summary
// tiles and panels.
import { useBaseStatistics } from "@/composables/useBaseStatistics";
import { useSeverity } from "@/composables/useSeverity";
import { useToCSV } from "@/composables/useToCSV";
import { ccService, handleThriftError } from "@cc-api";
import {
  Checker,
  Guideline,
  ReportFilter,
  RunFilter,
  Severity
} from "@cc/report-server-types";
import { computed, ref, watch } from "vue";
import StatisticsDialog from "@/components/Statistics/StatisticsDialog";
import GuidelineStatisticsTable
  from "@/components/Statistics/Guideline/GuidelineStatisticsTable";
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
const toCSV = useToCSV();
const baseStatistics = useBaseStatistics(props, null);

const guidelineOptions = ref([
  {
    id: "sei-cert-c",
    name: "SEI CERT Coding Standard (C)",
    value: 0
  },
  {
    id: "sei-cert-cpp",
    name: "SEI CERT Coding Standard (C++)",
    value: 1
  },
  {
    id: "cwe-vulnerabilities",
    name: "CWE Vulnerabilities",
    value: 2
  },
  {
    id: "cwe-top-25-2024",
    name: "CWE Top 25 Most Dangerous Software Weaknesses 2024",
    value: 3
  },
  {
    id: "cwe-top-25-2025",
    name: "CWE Top 25 Most Dangerous Software Weaknesses 2025",
    value: 4
  },
  {
    id: "owasp-top-10-2021",
    name: "OWASP Top 10 Web Application Security Risks 2021",
    value: 5
  },
  {
    id: "owasp-top-10-2025",
    name: "OWASP Top 10 Web Application Security Risks 2025",
    value: 6
  },
  {
    id: "memory-safety",
    name: "Memory-safety related CWEs",
    value: 7
  }
]);
const all_guideline_rules = ref({});
const checker_stats = ref({});
const loading = ref(false);
const noProperRun = ref(false);
const runs = ref(null);
const runData = ref([]);
const selectedCheckerName = ref(null);
const selectedGuidelineIndex = ref(0);
const showRuns = ref({
  enabled: false,
  disabled: false,
  unknown: false,
});
const statistics = ref([]);
const type = ref(null);
const hideNotOutstanding = ref(false);

const selectedGuideline = computed(() =>
  new Guideline({
    guidelineName: guidelineOptions.value[selectedGuidelineIndex.value].id
  })
);

// The key numbers above the table, for all rules of the guideline.
const summaryTiles = computed(() => {
  const checkers = statistics.value.flatMap(rule => rule.checkers);
  const sum = field =>
    checkers.reduce((total, checker) => total + checker[field], 0);
  return [
    {
      label: "Rules",
      value: statistics.value.length,
      icon: "mdi-format-list-numbered",
      tone: TONES.neutral
    },
    {
      label: "Non-compliant rules",
      value: statistics.value.filter(rule =>
        rule.checkers.some(checker => checker.outstanding > 0)).length,
      icon: "mdi-alert-octagon-outline",
      tone: TONES.outstanding
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

const filteredStatistics = computed(() => {
  if (hideNotOutstanding.value) {
    return statistics.value.filter(stat =>
      stat.checkers.some(checker => checker.outstanding > 0)
    );
  }
  return statistics.value;
});

// Refresh the data on ReportFilter changes
baseStatistics.setupRefreshListener(fetchStatistics);

watch(() => baseStatistics.runIds, async () => {
  noProperRun.value = false;
});

watch(selectedGuideline, async () => {
  await fetchStatistics();
});

function checker_stat(stat) {
  statistics.value = [];
  Object.keys(all_guideline_rules.value).forEach(
    guideline => {
      statistics.value.push(
        ...all_guideline_rules.value[guideline].map(rule => {
          const filtered_stat = Object.keys(stat).filter(
            checkerId => rule.checkers.map(c => c.checkerName).includes(
              stat[checkerId].checkerName));

          const matchedCheckerNames = new Set(
            filtered_stat.map(checkerId => stat[checkerId].checkerName)
          );

          const matchedCheckers = filtered_stat.map(checkerId => {
            return {
              name: stat[checkerId].checkerName,
              severity: stat[checkerId].severity,
              enabledInAllRuns: stat[checkerId].disabled.length === 0
                ? 1
                : 0,
              enabledRunLength: stat[checkerId].enabled.length,
              disabledRunLength: stat[checkerId].disabled.length,
              unknownRunLength: stat[checkerId].unknown.length,
              closed: stat[checkerId].closed.toNumber(),
              outstanding: stat[checkerId].outstanding.toNumber(),
            };
          });

          const missingCheckers = rule.checkers
            .filter(checker => !matchedCheckerNames.has(checker.checkerName))
            .map(checker => {
              return {
                name: checker.checkerName,
                severity: checker.severity !== null
                  ? severity.severityFromStringToCode(checker.severity)
                  : Severity.UNSPECIFIED,
                enabledInAllRuns: 0,
                enabledRunLength: 0,
                disabledRunLength: runs.value.length,
                unknownRunLength: 0,
                closed: 0,
                outstanding: 0,
              };
            });

          return {
            guidelineName: guideline,
            guidelineRule: rule.ruleId,
            guidelineUrl: rule.url,
            guidelineRuleTitle: rule.title,
            guidelineLevel: rule.level,
            checkers: [ ...matchedCheckers, ...missingCheckers ]
          };
        })
      );
    });
}

function downloadCSV() {
  const _values = [];
  statistics.value.forEach(stat => {
    stat.checkers.forEach(checker => {
      const _value = [
        stat.guidelineName,
        stat.guidelineRule,
        stat.guidelineRuleTitle,
        checker.name,
        severity.severityFromCodeToString(checker.severity),
        checker.enabledInAllRuns
          ? "Enabled in all selected runs"
          : "Not enabled in all selected runs",
        checker.closed,
        checker.outstanding
      ];

      _values.push(_value);
    });
  });

  const _data = [
    [
      "Guideline Name", "Rule Name", "Rule Title", "Related Checker(s)",
      "Checker Severity", "Checker Status", "Closed Reports",
      "Outstanding Reports"
    ],
    ..._values
  ];

  toCSV.toCSV(_data, "codechecker_guideline_statistics.csv");
}

async function getAllGuidelineRules() {
  all_guideline_rules.value = await new Promise(resolve => {
    ccService.getClient().getGuidelineRules(
      [ selectedGuideline.value ],
      handleThriftError(async guidelines => {
        for (const [ guideline, rules ] of Object.entries(guidelines)) {
          const _all_checkers = [];
          rules.forEach(rule => {
            rule.checkers.map(checker => {
              const chk = new Checker({
                analyzerName: null,
                checkerId: checker
              });

              if (!_all_checkers.some(
                c => c.checkerId === chk.checkerId)) {
                _all_checkers.push(chk);
              }
            });
          });

          guidelines[guideline] = rules.map(rule => {
            return {
              ruleId: rule.ruleId,
              title: rule.title,
              url: rule.url,
              checkers: rule.checkers.map(
                str =>
                  ({ checkerName:str, severity:null })
              ),
              level: rule.level
            };
          });
        }

        resolve(guidelines);
      })
    );
  });
}

async function fetchStatistics() {
  loading.value = true;

  const _filter = new RunFilter({
    ids: baseStatistics.runIds.value
  });

  runs.value = await ccService.getRuns(_filter);

  await getAllGuidelineRules();

  const filter = new ReportFilter(baseStatistics.reportFilter.value);

  const checker_stat_result = await new Promise(resolve => {
    ccService.getClient().getCheckerStatusVerificationDetails(
      baseStatistics.runIds.value,
      filter,
      handleThriftError(res => {
        resolve(res);
      }));
  });

  checker_stats.value = checker_stat_result;
  checker_stat(checker_stat_result);
  loading.value = false;
}

function showingRuns(_type, _checker_name) {
  type.value = _type;
  selectedCheckerName.value = _checker_name;

  const _checker_id = Object.keys(checker_stats.value).find(_checker_id =>
    checker_stats.value[_checker_id].checkerName === _checker_name
  );

  if (_checker_id) {
    runData.value = checker_stats.value[_checker_id][_type].map(
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

</script>

<style>
  .selection-item {
    display: block;
    width: 100%;
  }
</style>
