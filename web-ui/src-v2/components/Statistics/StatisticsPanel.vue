<template>
  <section
    class="cc-panel"
    :class="{ 'cc-panel--flush': flush }"
    :style="{
      '--cc-tone': tone,
      '--cc-delay': `calc(var(--cc-motion-step) * ${delay})`
    }"
  >
    <header class="cc-panel__header">
      <span class="cc-panel__icon">
        <v-icon :icon="icon" size="20" />
      </span>
      <div class="cc-panel__titles">
        <h3 class="cc-panel__title">
          {{ title }}
          <tooltip-help-icon
            v-if="$slots.help"
            color="grey"
            size="x-small"
          >
            <slot name="help" />
          </tooltip-help-icon>
        </h3>
        <div v-if="subtitle" class="cc-panel__subtitle">
          {{ subtitle }}
        </div>
      </div>
      <div v-if="$slots.actions" class="cc-panel__actions">
        <slot name="actions" />
      </div>
    </header>

    <div v-if="$slots.toolbar" class="cc-panel__toolbar">
      <slot name="toolbar" />
    </div>

    <div class="cc-panel__body">
      <slot />
    </div>
  </section>
</template>

<script setup>
// A card of the v2 statistics pages: a tinted icon, a title with an
// optional help tooltip and subtitle, actions on the right, and the
// content. With `flush`, a table inside reaches the card's edges.

import TooltipHelpIcon from "@/components/TooltipHelpIcon";

defineProps({
  icon: { type: String, required: true },
  title: { type: String, required: true },
  subtitle: { type: String, default: null },
  tone: { type: String, default: "rgb(var(--v-theme-primary))" },
  flush: { type: Boolean, default: false },
  // Entrance delay in stagger steps (--cc-motion-step), for panels that
  // appear one after the other.
  delay: { type: Number, default: 0 }
});
</script>

<style lang="scss" scoped>
.cc-panel {
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  overflow: hidden;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay);
}

.cc-panel__header {
  display: flex;
  flex-wrap: wrap;
  align-items: flex-start;
  gap: 12px;
  padding: 18px 20px 14px;
}

.cc-panel__icon {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: color-mix(in srgb, var(--cc-tone) 12%, transparent);
  color: var(--cc-tone);
}

.cc-panel__titles {
  flex: 1 1 240px;
  min-width: 0;
}

.cc-panel__title {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0;
  font-size: 1rem;
  font-weight: 650;
  letter-spacing: -.01em;
}

.cc-panel__subtitle {
  font-size: .8rem;
  color: var(--cc-muted);
}

.cc-panel__actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
}

.cc-panel__toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px 20px;
  padding: 0 20px 14px;
}

.cc-panel__body {
  padding: 0 20px 20px;
}

// Tables reach the card's edges, under a line.
.cc-panel--flush .cc-panel__body {
  padding: 0;
  border-top: 1px solid var(--cc-border);

  :deep(table) {
    border: none !important;
  }
}
</style>
