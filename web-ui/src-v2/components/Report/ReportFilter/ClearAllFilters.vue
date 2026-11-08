<template>
  <v-btn
    id="clear-all-filter-btn"
    class="cc-filter-clear"
    color="primary"
    variant="tonal"
    height="32"
    prepend-icon="mdi-filter-remove-outline"
    title="Clear all filters"
    @click="emit('clear')"
  >
    Clear all
  </v-btn>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportFilter/ClearAllFilters.vue
// (see vite.config.mjs). A small button in the filter sidebar's header.

const emit = defineEmits([ "clear" ]);
</script>

<style lang="scss" scoped>
.cc-filter-clear {
  flex: 0 0 auto;
  font-size: .8rem;
}
</style>
