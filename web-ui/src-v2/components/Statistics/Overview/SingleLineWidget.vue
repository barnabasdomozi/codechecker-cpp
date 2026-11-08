<template>
  <v-card
    class="cc-kpi"
    :style="{ '--cc-tone': color }"
    :loading="loading"
    flat
  >
    <span class="cc-kpi__icon">
      <v-icon :icon="icon" size="22" />
    </span>

    <div class="cc-kpi__body">
      <div class="cc-kpi__label">
        {{ label }}
        <tooltip-help-icon color="grey" size="x-small">
          <slot name="help" />
        </tooltip-help-icon>
      </div>

      <div
        v-if="value === null"
        class="cc-kpi__placeholder"
      />
      <div
        v-else
        class="cc-kpi__value"
      >
        <slot
          name="value"
          :value="value"
        >
          {{ value.toLocaleString() }}
        </slot>

        <slot name="append-value" />
      </div>
    </div>
  </v-card>
</template>

<script setup>
// Web UI v2: replaces SingleLineWidget.vue of src/ (see vite.config.mjs),
// with the same props and slots.

import TooltipHelpIcon from "@/components/TooltipHelpIcon";

defineProps({
  icon: { type: String, required: true },
  color: { type: String, required: true },
  label: { type: String, required: true },
  helpMessage: { type: String, default: null },
  bus: { type: Object, required: true },
  value: { type: Number, default: null },
  loading: { type: Boolean, default: false }
});
</script>

<style lang="scss" scoped>
.cc-kpi {
  display: flex;
  align-items: center;
  gap: 16px;
  height: 100%;
  padding: 18px 20px;
  border-radius: var(--cc-radius-lg) !important;
  border: 1px solid var(--cc-border) !important;
  box-shadow: var(--cc-shadow-sm) !important;
}

// A tinted tile in the widget's color.
.cc-kpi__icon {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 44px;
  height: 44px;
  border-radius: 12px;
  background: color-mix(in srgb, var(--cc-tone) 12%, transparent);
  color: var(--cc-tone);
}

.cc-kpi__body {
  min-width: 0;
}

.cc-kpi__label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: .8rem;
  font-weight: 600;
  color: var(--cc-muted);
}

.cc-kpi__value {
  font-size: 1.75rem;
  font-weight: 700;
  line-height: 1.2;
  letter-spacing: -.02em;
  font-variant-numeric: tabular-nums;
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;
}

// Shimmers in place of the number while it loads.
.cc-kpi__placeholder {
  position: relative;
  width: 64px;
  height: 32px;
  margin-top: 2px;
  overflow: hidden;
  border-radius: 6px;
  background: rgba(var(--v-theme-on-surface), .07);

  &::after {
    content: "";
    position: absolute;
    inset: 0;
    background: linear-gradient(
      90deg, transparent, rgba(var(--v-theme-on-surface), .08), transparent
    );
    animation: cc-shimmer 1.2s ease-in-out infinite;
  }
}
</style>
