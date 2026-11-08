<template>
  <v-dialog
    v-model="dialog"
    max-width="1080px"
    scrollable
  >
    <template v-slot:activator="{ props: activatorProps }">
      <v-btn
        v-if="iconOnly"
        v-bind="activatorProps"
        id="show-analysis-info-btn"
        class="show-analysis-info"
        color="primary"
        :variant="iconVariant"
        :size="iconSize"
        icon="mdi-console"
        title="Analysis info"
      />
      <v-btn
        v-else
        v-bind="activatorProps"
        id="show-analysis-info-btn"
        class="show-analysis-info"
        color="primary"
        :variant="iconVariant"
        :size="iconSize"
        prepend-icon="mdi-console"
      >
        Analysis Info
      </v-btn>
    </template>

    <v-card class="analysis-info cc-ai">
      <header class="cc-ai__header">
        <span class="cc-ai__header-icon">
          <v-icon icon="mdi-console" size="20" />
        </span>
        <div class="cc-ai__header-text">
          <div class="cc-ai__title">
            Analysis info
          </div>
          <div class="cc-ai__subtitle">
            {{ subtitle }}
          </div>
        </div>
        <v-btn
          class="close-btn"
          icon="mdi-close"
          size="small"
          variant="text"
          title="Close"
          @click="dialog = false"
        />
      </header>

      <v-progress-linear
        :active="loading"
        indeterminate
        height="2"
      />

      <div class="cc-ai__body">
        <!-- Loading -->
        <div v-if="loading" class="cc-ai__skeleton">
          <div class="cc-ai__stats">
            <div
              v-for="i in 3"
              :key="i"
              class="cc-ai__stat cc-ai__shimmer"
            />
          </div>
          <div class="cc-ai__shimmer cc-ai__shimmer--cmd" />
          <div class="cc-ai__shimmer cc-ai__shimmer--list" />
        </div>

        <template v-else-if="analysisInfo">
          <!-- Summary -->
          <div v-if="checkersAvailable" class="cc-ai__stats">
            <div
              v-for="(stat, idx) in stats"
              :key="stat.label"
              class="cc-ai__stat cc-rise"
              :style="{ '--cc-delay': `calc(${idx} * var(--cc-motion-step))` }"
            >
              <span
                class="cc-ai__stat-icon"
                :class="`cc-ai__stat-icon--${stat.tone}`"
              >
                <v-icon :icon="stat.icon" size="18" />
              </span>
              <div class="cc-ai__stat-text">
                <div class="cc-ai__stat-value">
                  {{ stat.value }}
                  <span v-if="stat.of" class="cc-ai__stat-of">
                    / {{ stat.of }}
                  </span>
                </div>
                <div class="cc-ai__stat-label">
                  {{ stat.label }}
                </div>
              </div>
            </div>
          </div>

          <!-- Analysis commands -->
          <section class="cc-ai__section cc-rise">
            <div class="cc-ai__section-head">
              <h3 class="cc-ai__section-title">
                {{ commands.length > 1
                  ? "Analysis commands" : "Analysis command" }}
              </h3>
              <div v-if="legend.length" class="cc-ai__legend">
                <span
                  v-for="item in legend"
                  :key="item.kind"
                  class="cc-ai__legend-item"
                >
                  <span
                    class="cc-ai__legend-dot"
                    :class="`cc-ai__tok--${item.kind}`"
                  />
                  {{ item.label }}
                </span>
              </div>
            </div>

            <div
              v-if="commands.length"
              class="cc-ai__commands analyze-command-container"
            >
              <div
                v-for="(cmd, idx) in commands"
                :key="idx"
                class="cc-ai__command analyze-command"
              >
                <pre class="cc-ai__command-text"><span
                  v-for="(tok, tIdx) in cmd.tokens"
                  :key="tIdx"
                  :class="tok.kind
                    ? `cc-ai__tok cc-ai__tok--${tok.kind}` : null"
                >{{ tok.text }}</span></pre>
                <v-btn
                  class="cc-ai__copy"
                  size="small"
                  variant="text"
                  :icon="copiedIdx === idx ? 'mdi-check' : 'mdi-content-copy'"
                  :color="copiedIdx === idx ? 'success' : undefined"
                  :title="copiedIdx === idx ? 'Copied' : 'Copy command'"
                  @click="copyCommand(cmd.raw, idx)"
                />
              </div>
            </div>
            <div v-else class="cc-ai__empty">
              No analysis command was stored for this analysis.
            </div>
          </section>

          <!-- Checkers -->
          <section
            v-if="checkersAvailable"
            class="cc-ai__section cc-ai__section--checkers cc-rise"
            style="--cc-delay: calc(2 * var(--cc-motion-step))"
          >
            <div class="cc-ai__section-head">
              <h3 class="cc-ai__section-title">
                Checkers
              </h3>
              <div class="cc-ai__tools">
                <v-text-field
                  v-model="search"
                  class="cc-ai__search"
                  density="compact"
                  variant="outlined"
                  hide-details
                  clearable
                  prepend-inner-icon="mdi-magnify"
                  placeholder="Search checkers"
                />
                <v-btn-toggle
                  v-model="statusFilter"
                  class="cc-ai__status-toggle"
                  density="compact"
                  mandatory
                >
                  <v-btn value="all" size="small">
                    All
                  </v-btn>
                  <v-btn value="enabled" size="small">
                    Enabled
                  </v-btn>
                  <v-btn value="disabled" size="small">
                    Disabled
                  </v-btn>
                </v-btn-toggle>
              </div>
            </div>

            <div class="cc-ai__checkers">
              <!-- Analyzers -->
              <nav class="cc-ai__analyzers" aria-label="Analyzers">
                <button
                  v-for="analyzer in analyzerSummaries"
                  :key="analyzer.name"
                  type="button"
                  class="cc-ai__analyzer analyzer-checkers-panel"
                  :class="{
                    'cc-ai__analyzer--active': analyzer.name === activeAnalyzer
                  }"
                  :data-analyzer-name="analyzer.name"
                  :aria-pressed="analyzer.name === activeAnalyzer"
                  @click="activeAnalyzer = analyzer.name"
                >
                  <span class="cc-ai__analyzer-row">
                    <span class="cc-ai__analyzer-name analyzer-name">
                      {{ analyzer.name }}
                    </span>
                    <span
                      v-if="filtering"
                      class="cc-ai__analyzer-matches"
                      :title="`${analyzer.matches} matching checkers`"
                    >
                      {{ analyzer.matches }}
                    </span>
                  </span>
                  <span class="cc-ai__analyzer-count">
                    <b>{{ analyzer.enabled }}</b>
                    of {{ analyzer.total }} enabled
                  </span>
                  <span class="cc-ai__bar">
                    <span
                      class="cc-ai__bar-fill"
                      :style="{
                        width: percent(analyzer.enabled, analyzer.total)
                      }"
                    />
                  </span>
                </button>
              </nav>

              <!-- Groups of the active analyzer -->
              <div class="cc-ai__groups">
                <div class="cc-ai__groups-head">
                  <span>
                    {{ visibleGroups.length }}
                    {{ visibleGroups.length === 1 ? "group" : "groups" }},
                    {{ visibleCheckerCount }}
                    {{ visibleCheckerCount === 1 ? "checker" : "checkers" }}
                  </span>
                  <v-btn
                    v-if="visibleGroups.length > 1 && !search"
                    size="small"
                    variant="text"
                    color="primary"
                    @click="toggleAllGroups"
                  >
                    {{ allOpen ? "Collapse all" : "Expand all" }}
                  </v-btn>
                </div>

                <div
                  v-for="group in visibleGroups"
                  :key="`${activeAnalyzer}/${group.name}`"
                  class="cc-ai__group analyzer-checker-group-panel"
                  :class="{ 'cc-ai__group--open': isOpen(group) }"
                  :data-group-name="group.name"
                >
                  <button
                    type="button"
                    class="cc-ai__group-head"
                    :aria-expanded="isOpen(group)"
                    @click="toggleGroup(group)"
                  >
                    <v-icon
                      class="cc-ai__chevron"
                      icon="mdi-chevron-right"
                      size="18"
                    />
                    <v-icon
                      class="cc-ai__group-status"
                      :class="`cc-ai__group-status--${group.status}`"
                      :icon="statusIcons[group.status]"
                      :title="statusTitles[group.status]"
                      size="16"
                    />
                    <span class="cc-ai__group-name checker-group-name">
                      {{ group.label }}
                    </span>
                    <span class="cc-ai__group-count">
                      {{ group.enabled }}<span
                        class="cc-muted"
                      >/{{ group.total }}</span>
                    </span>
                    <span class="cc-ai__bar cc-ai__bar--group">
                      <span
                        class="cc-ai__bar-fill"
                        :style="{ width: percent(group.enabled, group.total) }"
                      />
                    </span>
                  </button>

                  <div v-if="isOpen(group)" class="cc-ai__checker-grid">
                    <div
                      v-for="checker in group.checkers"
                      :key="checker.name"
                      class="cc-ai__checker analysis-info-checker"
                      :class="checker.enabled
                        ? 'cc-ai__checker--enabled'
                        : 'cc-ai__checker--disabled'"
                      :data-checker-name="checker.name"
                      :title="`'${checker.name}' ${checker.enabled
                        ? 'was' : 'was not'} enabled in this analysis`"
                    >
                      <v-icon
                        :icon="checker.enabled
                          ? 'mdi-check-circle' : 'mdi-minus-circle-outline'"
                        size="15"
                      />
                      <span class="cc-ai__checker-name checker-name">
                        <template
                          v-for="(part, pIdx) in highlight(checker.name)"
                          :key="pIdx"
                        >
                          <mark v-if="part.match">{{ part.text }}</mark>
                          <template v-else>{{ part.text }}</template>
                        </template>
                      </span>
                    </div>
                  </div>
                </div>

                <div v-if="!visibleGroups.length" class="cc-ai__empty">
                  No checkers of {{ activeAnalyzer }} match the filter.
                </div>
              </div>
            </div>
          </section>

          <!-- No checker list -->
          <div
            v-else-if="unavailableReason"
            class="cc-ai__notice checker-status-unavailable cc-rise"
          >
            <v-icon icon="mdi-alert-circle-outline" size="20" />
            <div>
              <div class="cc-ai__notice-title">
                The list of checkers is not available
              </div>
              <div v-if="unavailableReason === 'unknown'">
                This is likely because the run was stored from a report
                directory that was not created by
                <code>CodeChecker analyze</code>. Using the
                <code>report-converter</code> on the results of third-party
                analyzers can cause this, as CodeChecker does not know the
                analysis configuration then.
              </div>
              <div v-else-if="unavailableReason === 'version'">
                The list of executed checkers is available from CodeChecker
                <b>6.24</b>. This analysis was run with an older client
                (<code>{{ analysisInfo.codeCheckerVersion }}</code>), and was
                likely stored when the server ran that older version, too.
              </div>
              <div v-else-if="unavailableReason === 'report'">
                The checkers executed by the analysis that produced this
                report cannot be queried. Open the analysis info of the run
                to see them.
              </div>
            </div>
          </div>
        </template>
      </div>
    </v-card>
  </v-dialog>
</template>

<script setup>
// Web UI v2: replaces src/components/AnalysisInfoDialog.vue (see
// vite.config.mjs). The analysis info of a run, a run history entry or a
// report: a summary of the checkers, the analysis commands with their
// options highlighted, and the checkers of each analyzer by group, with a
// search and an enabled / disabled filter. The props and the loading logic
// (src/composables/useAnalysisInfo.js) are the original ones.

import { computed, markRaw, reactive, ref, shallowRef, watch } from "vue";

import {
  CheckerInfoAvailability,
  CountKeys,
  GroupKeys,
  decideNegativeCheckerStatusAvailability,
  useAnalysisInfo,
} from "@/composables/useAnalysisInfo";

const props = defineProps({
  runId: { type: Object, default: () => null },
  runHistoryId: { type: Object, default: () => null },
  reportId: { type: Object, default: () => null },
  iconOnly: { type: Boolean, default: false },
  iconSize: { type: String, default: "small" }
});

const dialog = ref(false);
const loading = ref(false);
// Reactive, because decideNegativeCheckerStatusAvailability() may change
// the availability over the API after the dialog shows the result.
const analysisInfo = shallowRef(null);
const availability = computed(() =>
  analysisInfo.value?.checkerInfoAvailability
    ?? CheckerInfoAvailability.Unloaded);
const commands = shallowRef([]);
const copiedIdx = ref(null);

const activeAnalyzer = ref(null);
const search = ref("");
const statusFilter = ref("all");
const openGroups = ref(new Set());

const analysisInfoComp = useAnalysisInfo();

const iconVariant = computed(() => props.iconOnly ? "text" : "outlined");

const subtitle = computed(() => {
  if (props.reportId) return "The analysis that produced this report";
  if (props.runHistoryId) return "The analysis of this run history entry";
  return "The analyses of this run";
});

watch(dialog, open => {
  if (open) getAnalysisInfo();
});

let loadId = 0;

async function getAnalysisInfo() {
  if (!props.runId && !props.runHistoryId && !props.reportId) return;

  const id = ++loadId;
  loading.value = true;
  analysisInfo.value = null;
  search.value = "";
  statusFilter.value = "all";
  openGroups.value = new Set();

  const raw = await analysisInfoComp.loadAnalysisInfo(
    props.runId, props.runHistoryId, props.reportId);
  if (id !== loadId) return;

  // The checker lists are only read: they need no deep reactivity.
  raw.groupAndCountCheckers();
  markRaw(raw.checkers);
  markRaw(raw.checkersGroupedAndSorted);
  markRaw(raw.checkerGroupCounts);
  const info = reactive(raw);

  decideNegativeCheckerStatusAvailability(
    info, props.runId, props.runHistoryId, props.reportId);

  commands.value = info.cmds.map(cmd => ({
    raw: cmd,
    tokens: tokenizeCommand(cmd)
  }));
  analysisInfo.value = info;
  activeAnalyzer.value = info.analyzers[0] ?? null;
  loading.value = false;
}

// Commands -----------------------------------------------------------------

const ENABLE_RGX = /^(--enable|-e(?=$|=))/i;
const DISABLE_RGX = /^(--disable|-d(?=$|=))/i;

const LEGEND = {
  enabled: "Enabled checkers",
  disabled: "Disabled checkers",
  ctu: "Cross translation unit",
  stats: "Statistics",
  param: "Options"
};

// Splits a command into words and spaces. Options get a kind, and so does
// the value after --enable / --disable when it is a separate word.
function tokenizeCommand(cmd) {
  const tokens = [];
  let valueKind = null;

  for (const text of cmd.split(/(\s+)/)) {
    if (!text) continue;
    if (/^\s+$/.test(text)) {
      tokens.push({ text, kind: null });
      continue;
    }

    if (!text.startsWith("-")) {
      tokens.push({ text, kind: valueKind });
      valueKind = null;
      continue;
    }

    let kind = "param";
    if (ENABLE_RGX.test(text)) kind = "enabled";
    else if (DISABLE_RGX.test(text)) kind = "disabled";
    else if (text.startsWith("--ctu")) kind = "ctu";
    else if (text.startsWith("--stats")) kind = "stats";

    tokens.push({ text, kind });
    valueKind = (kind === "enabled" || kind === "disabled")
      && !text.includes("=") ? kind : null;
  }

  return tokens;
}

const legend = computed(() => {
  const kinds = new Set(commands.value.flatMap(
    cmd => cmd.tokens.map(tok => tok.kind)));
  return Object.entries(LEGEND)
    .filter(([ kind ]) => kinds.has(kind))
    .map(([ kind, label ]) => ({ kind, label }));
});

async function copyCommand(text, idx) {
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
  copiedIdx.value = idx;
  setTimeout(() => {
    if (copiedIdx.value === idx) copiedIdx.value = null;
  }, 1200);
}

// Checkers -----------------------------------------------------------------

const checkersAvailable = computed(() =>
  availability.value === CheckerInfoAvailability.Available);

const unavailableReason = computed(() => {
  switch (availability.value) {
  case CheckerInfoAvailability.UnknownReason:
    return "unknown";
  case CheckerInfoAvailability.RunHistoryStoredWithOldVersionPre_v6_24:
    return "version";
  case CheckerInfoAvailability.ReportIdToAnalysisInfoIdQueryNontrivialOverAPI:
    return "report";
  default:
    return null;
  }
});

const filtering = computed(() =>
  !!search.value || statusFilter.value !== "all");

function matchesFilter(name, enabled) {
  if (statusFilter.value === "enabled" && !enabled) return false;
  if (statusFilter.value === "disabled" && enabled) return false;
  const q = search.value?.trim().toLowerCase();
  return !q || name.toLowerCase().includes(q);
}

function totals(analyzer) {
  const counts = analysisInfo.value.checkerGroupCounts[analyzer];
  return counts[GroupKeys.AnalyzerTotal];
}

const totalCounts = computed(() => {
  const info = analysisInfo.value;
  if (!info || !checkersAvailable.value) return null;
  return info.analyzers.reduce((sum, analyzer) => {
    const t = totals(analyzer);
    return {
      enabled: sum.enabled + t[CountKeys.Enabled],
      disabled: sum.disabled + t[CountKeys.Disabled],
      total: sum.total + t[CountKeys.Total]
    };
  }, { enabled: 0, disabled: 0, total: 0 });
});

const stats = computed(() => {
  const info = analysisInfo.value;
  const t = totalCounts.value;
  if (!t) return [];

  return [ {
    label: info.analyzers.length === 1 ? "Analyzer" : "Analyzers",
    value: info.analyzers.length,
    icon: "mdi-cog-outline",
    tone: "primary"
  }, {
    label: "Enabled checkers",
    value: t.enabled.toLocaleString(),
    of: t.total.toLocaleString(),
    icon: "mdi-check-circle-outline",
    tone: "success"
  }, {
    label: "Disabled checkers",
    value: t.disabled.toLocaleString(),
    icon: "mdi-minus-circle-outline",
    tone: "muted"
  } ];
});

const analyzerSummaries = computed(() => {
  const info = analysisInfo.value;
  if (!info || !checkersAvailable.value) return [];

  return info.analyzers.map(name => {
    const t = totals(name);
    let matches = 0;
    if (filtering.value) {
      for (const checkers of
        Object.values(info.checkersGroupedAndSorted[name])) {
        matches += checkers.filter(
          ([ checker, data ]) => matchesFilter(checker, data.enabled)).length;
      }
    }
    return {
      name,
      enabled: t[CountKeys.Enabled],
      total: t[CountKeys.Total],
      matches
    };
  });
});

const statusIcons = {
  all: "mdi-check-circle",
  partial: "mdi-circle-half-full",
  none: "mdi-close-circle-outline"
};

const statusTitles = {
  all: "Every checker of the group was enabled",
  partial: "Some checkers of the group were enabled",
  none: "No checker of the group was enabled"
};

// The groups of the active analyzer, with the checkers that match the
// filter. Checkers without a group come last.
const visibleGroups = computed(() => {
  const info = analysisInfo.value;
  const analyzer = activeAnalyzer.value;
  if (!info || !analyzer || !checkersAvailable.value) return [];

  const grouped = info.checkersGroupedAndSorted[analyzer] ?? {};
  const counts = info.checkerGroupCounts[analyzer];

  const groups = Object.entries(grouped).map(([ name, checkers ]) => {
    const c = counts[name];
    return {
      name,
      label: name === GroupKeys.NoGroup ? "Other checkers" : name,
      enabled: c[CountKeys.Enabled],
      total: c[CountKeys.Total],
      status: c[CountKeys.Disabled] === 0 ? "all"
        : c[CountKeys.Enabled] === 0 ? "none" : "partial",
      checkers: checkers
        .filter(([ checker, data ]) => matchesFilter(checker, data.enabled))
        .map(([ checker, data ]) => ({
          name: checker,
          enabled: data.enabled
        }))
    };
  }).filter(group => group.checkers.length);

  return [
    ...groups.filter(g => g.name !== GroupKeys.NoGroup),
    ...groups.filter(g => g.name === GroupKeys.NoGroup)
  ];
});

const visibleCheckerCount = computed(() =>
  visibleGroups.value.reduce((sum, g) => sum + g.checkers.length, 0));

function groupKey(group) {
  return `${activeAnalyzer.value}/${group.name}`;
}

// A search opens every group that has a match, as does a single group.
function isOpen(group) {
  return !!search.value || visibleGroups.value.length === 1 ||
    openGroups.value.has(groupKey(group));
}

function toggleGroup(group) {
  const key = groupKey(group);
  const next = new Set(openGroups.value);
  if (isOpen(group)) next.delete(key);
  else next.add(key);
  openGroups.value = next;
}

const allOpen = computed(() =>
  visibleGroups.value.every(group => isOpen(group)));

function toggleAllGroups() {
  const next = new Set(openGroups.value);
  const close = allOpen.value;
  for (const group of visibleGroups.value) {
    if (close) next.delete(groupKey(group));
    else next.add(groupKey(group));
  }
  openGroups.value = next;
}

// Splits a checker name around the searched text.
function highlight(name) {
  const q = search.value?.trim();
  if (!q) return [ { text: name, match: false } ];

  const parts = [];
  const lower = name.toLowerCase();
  const needle = q.toLowerCase();
  let pos = 0;
  let idx;
  while ((idx = lower.indexOf(needle, pos)) !== -1) {
    if (idx > pos) parts.push({ text: name.slice(pos, idx), match: false });
    parts.push({ text: name.slice(idx, idx + needle.length), match: true });
    pos = idx + needle.length;
  }
  if (pos < name.length) parts.push({ text: name.slice(pos), match: false });
  return parts;
}

function percent(part, total) {
  return total ? `${(part / total) * 100}%` : "0%";
}
</script>

<style lang="scss" scoped>
.cc-ai {
  display: flex;
  flex-direction: column;
  max-height: calc(100vh - 48px);
}

// Header.
.cc-ai__header {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  gap: 12px;
  padding: 16px 16px 14px 20px;
  border-bottom: 1px solid var(--cc-border);
}

.cc-ai__header-icon {
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

.cc-ai__header-text {
  flex: 1 1 auto;
  min-width: 0;
}

.cc-ai__title {
  font-size: 1.05rem;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.01em;
}

.cc-ai__subtitle {
  color: var(--cc-muted);
  font-size: .8rem;
}

.close-btn {
  color: var(--cc-muted);
}

// Body.
.cc-ai__body {
  flex: 1 1 auto;
  min-height: 0;
  overflow-y: auto;
  padding: 20px;
}

.cc-ai__section {
  margin-top: 24px;

  &:first-child {
    margin-top: 0;
  }
}

.cc-ai__section-head {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  gap: 8px 16px;
  margin-bottom: 10px;
}

.cc-ai__section-title {
  font-size: .72rem;
  font-weight: 650;
  text-transform: uppercase;
  letter-spacing: .06em;
  color: var(--cc-muted);
}

.cc-ai__empty {
  padding: 16px;
  border: 1px dashed var(--cc-border);
  border-radius: var(--cc-radius);
  color: var(--cc-muted);
  font-size: .85rem;
  text-align: center;
}

// Summary tiles.
.cc-ai__stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
}

.cc-ai__stat {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 64px;
  padding: 12px 14px;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius);
  background: rgba(var(--v-theme-on-surface), .02);
}

.cc-ai__stat-icon {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: 10px;

  &--primary {
    background: rgba(var(--v-theme-primary), .12);
    color: rgb(var(--v-theme-primary));
  }

  &--success {
    background: rgba(var(--v-theme-success), .14);
    color: rgb(var(--v-theme-success));
  }

  &--muted {
    background: rgba(var(--v-theme-on-surface), .06);
    color: var(--cc-muted);
  }
}

.cc-ai__stat-value {
  font-size: 1.2rem;
  font-weight: 700;
  line-height: 1.2;
  font-variant-numeric: tabular-nums;
}

.cc-ai__stat-of {
  color: var(--cc-muted);
  font-size: .85rem;
  font-weight: 550;
}

.cc-ai__stat-label {
  color: var(--cc-muted);
  font-size: .78rem;
}

// Commands.
.cc-ai__legend {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 14px;
  color: var(--cc-muted);
  font-size: .75rem;
}

.cc-ai__legend-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.cc-ai__legend-dot {
  width: 10px;
  height: 10px;
  border-radius: 3px;
}

.cc-ai__commands {
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 40vh;
  overflow-y: auto;
}

.cc-ai__command {
  position: relative;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius);
  background: rgb(var(--v-theme-surface-variant));
}

.cc-ai__command-text {
  margin: 0;
  padding: 12px 48px 12px 14px;
  font-family: var(--cc-font-mono);
  font-size: .8rem;
  line-height: 1.7;
  white-space: pre-wrap;
  word-break: break-word;
}

.cc-ai__copy {
  position: absolute;
  top: 6px;
  right: 6px;
  color: var(--cc-muted);
  opacity: .6;
  transition: opacity .15s ease;

  .cc-ai__command:hover &,
  &:focus-visible {
    opacity: 1;
  }
}

// Highlighted options; the legend uses the same colors.
.cc-ai__tok {
  padding: 1px 4px;
  border-radius: 4px;
  box-decoration-break: clone;
  -webkit-box-decoration-break: clone;

  &--param {
    background: rgba(var(--v-theme-on-surface), .07);
    font-weight: 600;
  }

  &--enabled {
    background: rgba(var(--v-theme-success), .18);
    color: rgb(var(--v-theme-success));
    font-weight: 600;
  }

  &--disabled {
    background: rgba(var(--v-theme-error), .14);
    color: rgb(var(--v-theme-error));
    font-weight: 600;
  }

  &--ctu,
  &--stats {
    background: rgba(var(--v-theme-primary), .14);
    color: rgb(var(--v-theme-primary));
    font-weight: 600;
  }
}

.cc-ai__legend-dot {
  &.cc-ai__tok--enabled { background: rgb(var(--v-theme-success)); }
  &.cc-ai__tok--disabled { background: rgb(var(--v-theme-error)); }
  &.cc-ai__tok--ctu,
  &.cc-ai__tok--stats { background: rgb(var(--v-theme-primary)); }
  &.cc-ai__tok--param { background: rgba(var(--v-theme-on-surface), .25); }
}

// Checker tools.
.cc-ai__tools {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
}

.cc-ai__search {
  width: 240px;
  max-width: 100%;

  :deep(.v-field) {
    height: 32px;
    font-size: .85rem;
  }

  :deep(.v-field__input) {
    min-height: 32px;
    padding-top: 0;
    padding-bottom: 0;
  }
}

.cc-ai__status-toggle {
  height: 32px !important;

  .v-btn {
    height: 32px !important;
    font-size: .8rem;
  }
}

// Analyzers and groups side by side.
.cc-ai__checkers {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr);
  gap: 16px;
  align-items: start;

  @media (max-width: 760px) {
    grid-template-columns: minmax(0, 1fr);
  }
}

.cc-ai__analyzers {
  display: flex;
  flex-direction: column;
  gap: 4px;
  position: sticky;
  top: 0;

  @media (max-width: 760px) {
    position: static;
    flex-direction: row;
    overflow-x: auto;
    padding-bottom: 4px;

    > .cc-ai__analyzer {
      flex: 0 0 180px;
    }
  }
}

.cc-ai__analyzer {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 10px 12px;
  border: 1px solid transparent;
  border-radius: var(--cc-radius-sm);
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
  transition: background-color .2s ease, border-color .2s ease;

  &:hover {
    background: rgba(var(--v-theme-primary), .05);
  }

  &:focus-visible {
    outline: none;
    box-shadow: inset 0 0 0 2px rgba(var(--v-theme-primary), .45);
  }

  &--active {
    border-color: rgba(var(--v-theme-primary), .3);
    background: rgba(var(--v-theme-primary), .08);

    .cc-ai__analyzer-name {
      color: rgb(var(--v-theme-primary));
    }
  }
}

.cc-ai__analyzer-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.cc-ai__analyzer-name {
  overflow: hidden;
  font-size: .9rem;
  font-weight: 650;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.cc-ai__analyzer-matches {
  flex: 0 0 auto;
  min-width: 22px;
  padding: 0 6px;
  border-radius: 999px;
  background: rgba(var(--v-theme-primary), .14);
  color: rgb(var(--v-theme-primary));
  font-size: .7rem;
  font-weight: 700;
  line-height: 18px;
  text-align: center;
}

.cc-ai__analyzer-count {
  color: var(--cc-muted);
  font-size: .75rem;

  b {
    color: rgb(var(--v-theme-on-surface));
    font-weight: 650;
  }
}

// Share of enabled checkers.
.cc-ai__bar {
  display: block;
  height: 4px;
  overflow: hidden;
  border-radius: 4px;
  background: rgba(var(--v-theme-on-surface), .08);

  &--group {
    flex: 0 0 64px;
  }
}

.cc-ai__bar-fill {
  display: block;
  height: 100%;
  border-radius: inherit;
  background: rgb(var(--v-theme-success));
  transition: width .4s var(--cc-ease-out);
}

// Groups.
.cc-ai__groups {
  min-width: 0;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius);
  overflow: hidden;
}

.cc-ai__groups-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 40px;
  padding: 4px 8px 4px 14px;
  border-bottom: 1px solid var(--cc-border);
  background: rgba(var(--v-theme-on-surface), .02);
  color: var(--cc-muted);
  font-size: .78rem;
}

.cc-ai__group + .cc-ai__group {
  border-top: 1px solid var(--cc-border);
}

.cc-ai__group-head {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  min-height: 40px;
  padding: 0 14px 0 8px;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
  transition: background-color .2s ease;

  &:hover {
    background: rgba(var(--v-theme-primary), .05);
  }

  &:focus-visible {
    outline: none;
    box-shadow: inset 0 0 0 2px rgba(var(--v-theme-primary), .45);
  }
}

.cc-ai__chevron {
  flex: 0 0 auto;
  color: var(--cc-muted);
  transition: transform .15s var(--cc-ease-out);

  .cc-ai__group--open & {
    transform: rotate(90deg);
  }
}

.cc-ai__group-status {
  flex: 0 0 auto;

  &--all { color: rgb(var(--v-theme-success)); }
  &--partial { color: rgb(var(--v-theme-primary)); }
  &--none { color: var(--cc-muted); }
}

.cc-ai__group-name {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  font-family: var(--cc-font-mono);
  font-size: .82rem;
  font-weight: 600;
  white-space: nowrap;
  text-overflow: ellipsis;

  .cc-ai__group--open & {
    color: rgb(var(--v-theme-primary));
  }
}

.cc-ai__group-count {
  flex: 0 0 auto;
  font-size: .78rem;
  font-weight: 650;
  font-variant-numeric: tabular-nums;
}

.cc-ai__checker-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 2px 16px;
  padding: 2px 14px 12px 34px;
  animation: cc-fade .18s ease both;
}

.cc-ai__checker {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  padding: 3px 0;

  .v-icon {
    flex: 0 0 auto;
  }

  &--enabled .v-icon {
    color: rgb(var(--v-theme-success));
  }

  &--disabled {
    color: var(--cc-muted);

    .v-icon {
      opacity: .7;
    }
  }
}

.cc-ai__checker-name {
  overflow: hidden;
  font-family: var(--cc-font-mono);
  font-size: .78rem;
  white-space: nowrap;
  text-overflow: ellipsis;

  mark {
    border-radius: 3px;
    background: rgba(var(--v-theme-warning), .3);
    color: inherit;
  }
}

// No checker list.
.cc-ai__notice {
  display: flex;
  gap: 12px;
  margin-top: 24px;
  padding: 14px 16px;
  border: 1px solid rgba(var(--v-theme-warning), .35);
  border-radius: var(--cc-radius);
  background: rgba(var(--v-theme-warning), .08);
  font-size: .85rem;
  line-height: 1.55;

  > .v-icon {
    flex: 0 0 auto;
    color: rgb(var(--v-theme-warning));
  }

  code {
    padding: 1px 5px;
    border-radius: 4px;
    background: rgba(var(--v-theme-on-surface), .07);
    font-size: .8rem;
  }
}

.cc-ai__notice-title {
  margin-bottom: 2px;
  font-weight: 650;
}

// Loading placeholders.
.cc-ai__shimmer {
  position: relative;
  overflow: hidden;
  border-radius: var(--cc-radius);
  background: rgba(var(--v-theme-on-surface), .05);

  &::after {
    content: "";
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, transparent,
      rgba(var(--v-theme-on-surface), .05), transparent);
    animation: cc-shimmer 1.2s ease-in-out infinite;
  }

  &--cmd {
    height: 72px;
    margin-top: 24px;
  }

  &--list {
    height: 220px;
    margin-top: 24px;
  }
}

.cc-ai__skeleton .cc-ai__stat {
  border-color: transparent;
}
</style>
