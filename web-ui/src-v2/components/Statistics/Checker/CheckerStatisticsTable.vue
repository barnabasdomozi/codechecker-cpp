<template>
  <base-statistics-table
    class="checker-statistics"
    v-bind="paginationAttrs(items.length)"
    :headers="headers"
    :items="items"
    :loading="loading"
    :mobile-breakpoint="1000"
    loading-text="Loading checker statistics..."
    item-key="checker"
    :sort-by="[{ key: 'severity', order: 'desc' }]"
    :necessary-total="true"
  />
</template>

<script setup>
// Web UI v2: replaces CheckerStatisticsTable.vue of src/ (see
// vite.config.mjs), with the column colors of
// src-v2/styles/_statistics-columns.scss and pages of rows (see
// ../pagination.js).

import { BaseStatisticsTable } from "@/components/Statistics";
import { paginationAttrs } from "@v2/components/Statistics/pagination";

defineProps({
  items: { type: Array, required: true },
  loading: { type: Boolean, default: false }
});

const headers = [
  {
    title: "Checker",
    key: "checker"
  },
  {
    title: "Severity",
    key: "severity",
    align: "center"
  },
  {
    title: "Unreviewed",
    key: "unreviewed.count",
    align: "center"
  },
  {
    title: "Confirmed bug",
    key: "confirmed.count",
    align: "center"
  },
  {
    title: "Outstanding reports",
    key: "outstanding.count",
    align: "center"
  },
  {
    title: "False positive",
    key: "falsePositive.count",
    align: "center"
  },
  {
    title: "Intentional",
    key: "intentional.count",
    align: "center"
  },
  {
    title: "Suppressed reports",
    key: "suppressed.count",
    align: "center"
  },
  {
    title: "All reports",
    key: "reports.count",
    align: "center"
  }
];
</script>

<style lang="scss">
@use "@v2/styles/statistics-columns" with (
  $class-name: ".checker-statistics"
);
</style>
