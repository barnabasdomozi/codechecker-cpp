<template>
  <div class="cc-filter-count" title="Number of reports">
    <span class="cc-filter-count__icon">
      <v-icon icon="mdi-filter-variant" size="20" />
    </span>
    <div class="cc-filter-count__body">
      <div class="cc-filter-count__title">
        Filters
      </div>
      <div class="cc-filter-count__reports">
        <span :key="value" class="cc-filter-count__value">
          {{ value.toLocaleString() }}
        </span>
        {{ value === 1 ? "report" : "reports" }}
      </div>
    </div>
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportFilter/ReportCount.vue
// (see vite.config.mjs). The title of the filter sidebar with the number
// of reports that the filters match; the number fades in when it changes.
// It stands first in the sidebar's header, before Clear all.

import { ref } from "vue";

defineProps({
  value: { type: Number, required: true }
});

const id = ref("report-count");

defineExpose({ id });
</script>

<style lang="scss" scoped>
.cc-filter-count {
  display: flex;
  flex: 1 1 auto;
  order: -1;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.cc-filter-count__icon {
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

.cc-filter-count__body {
  min-width: 0;
}

.cc-filter-count__title {
  font-size: 1rem;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.01em;
}

.cc-filter-count__reports {
  overflow: hidden;
  color: var(--cc-muted);
  font-size: .8rem;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.cc-filter-count__value {
  display: inline-block;
  color: rgb(var(--v-theme-on-surface));
  font-weight: 700;
  font-variant-numeric: tabular-nums;
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;
}
</style>
