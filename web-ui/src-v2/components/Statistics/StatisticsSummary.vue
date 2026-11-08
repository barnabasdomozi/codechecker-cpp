<template>
  <div class="cc-summary">
    <div
      v-for="(tile, i) in tiles"
      :key="tile.label"
      class="cc-summary__tile"
      :style="{
        '--cc-tone': tile.tone || 'rgb(var(--v-theme-primary))',
        '--cc-delay': `calc(var(--cc-motion-step) * ${i})`
      }"
    >
      <span class="cc-summary__icon">
        <v-icon :icon="tile.icon" size="20" />
      </span>
      <div class="cc-summary__body">
        <div class="cc-summary__label" :title="tile.label">
          {{ tile.label }}
        </div>
        <div
          v-if="loading || tile.value === null || tile.value === undefined"
          class="cc-summary__placeholder"
        />
        <div
          v-else
          class="cc-summary__value"
          :class="{ 'cc-summary__value--zero': tile.value === 0 }"
        >
          {{ tile.value.toLocaleString() }}
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
// The row of key numbers at the top of a v2 statistics page. Each tile is
// { label, value, icon, tone }; the tone colors the icon.

defineProps({
  tiles: { type: Array, required: true },
  loading: { type: Boolean, default: false }
});
</script>

<style lang="scss" scoped>
.cc-summary {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
  gap: 16px;
}

.cc-summary__tile {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px 18px;
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
  box-shadow: var(--cc-shadow-sm);
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay);
}

.cc-summary__icon {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  border-radius: 11px;
  background: color-mix(in srgb, var(--cc-tone) 12%, transparent);
  color: var(--cc-tone);
}

.cc-summary__body {
  min-width: 0;
}

.cc-summary__label {
  overflow: hidden;
  font-size: .78rem;
  font-weight: 600;
  white-space: nowrap;
  text-overflow: ellipsis;
  color: var(--cc-muted);
}

.cc-summary__value {
  font-size: 1.5rem;
  font-weight: 700;
  line-height: 1.2;
  letter-spacing: -.02em;
  font-variant-numeric: tabular-nums;
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;

  // A translucent color: the fade-in animation sets the opacity.
  &--zero {
    color: rgba(var(--v-theme-on-surface), .4);
  }
}

// Shimmers in place of the number while it loads.
.cc-summary__placeholder {
  position: relative;
  width: 56px;
  height: 28px;
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
