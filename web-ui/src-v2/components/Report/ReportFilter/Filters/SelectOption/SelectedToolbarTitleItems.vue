<template>
  <span
    v-if="selectedCount"
    class="cc-filter-badge"
    :title="title"
  >
    {{ selectedCount }}
  </span>
</template>

<script setup>
// Web UI v2: replaces SelectOption/SelectedToolbarTitleItems.vue of src/
// (see vite.config.mjs). The number of selected values in a filter's row,
// as a small badge; a filter without values shows none.

import { computed } from "vue";

const props = defineProps({
  value: { type: Array, default: () => [] },
});

const selectedCount = computed(() =>
  Array.isArray(props.value) ? props.value.length : 1
);

const title = computed(() => {
  const titles = Array.isArray(props.value)
    ? props.value.map(i => i.title).join(", ")
    : "";
  return titles ? `Selected: ${titles}.` : "No filter items are selected.";
});
</script>

<style lang="scss" scoped>
.cc-filter-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 20px;
  height: 20px;
  padding: 0 6px;
  border-radius: 10px;
  background: rgba(var(--v-theme-primary), .12);
  color: rgb(var(--v-theme-primary));
  font-size: .72rem;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
  animation: cc-pop .3s var(--cc-ease-out) both;
}
</style>
