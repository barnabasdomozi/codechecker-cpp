<template>
  <v-btn
    class="cc-stat-action"
    color="primary"
    variant="tonal"
    prepend-icon="mdi-download-outline"
    height="36"
    @click="emit('export')"
  >
    Export CSV
  </v-btn>
  <v-btn
    class="cc-stat-action cc-stat-action--icon"
    icon
    title="Reload statistics"
    variant="text"
    @click="emit('refresh')"
  >
    <v-icon
      icon="mdi-refresh"
      :class="{ 'cc-stat-action__spin': loading }"
    />
  </v-btn>
</template>

<script setup>
// The Export CSV and Reload buttons of a statistics panel. The reload icon
// spins while the statistics load.

defineProps({
  loading: { type: Boolean, default: false }
});

const emit = defineEmits([ "export", "refresh" ]);
</script>

<style lang="scss" scoped>
.cc-stat-action--icon {
  width: 36px !important;
  height: 36px !important;
  color: var(--cc-muted);
}

.cc-stat-action__spin {
  animation: cc-spin .8s linear infinite;
}
</style>
