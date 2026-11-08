<template>
  <section
    class="cc-filter-section"
    :class="{ 'cc-filter-section--open': panelOpen }"
  >
    <div
      class="cc-filter-section__head"
      role="button"
      tabindex="0"
      :aria-expanded="panelOpen"
      @click="panelOpen = !panelOpen"
      @keydown.enter.self.prevent="panelOpen = !panelOpen"
      @keydown.space.self.prevent="panelOpen = !panelOpen"
    >
      <v-icon
        class="expansion-btn cc-filter-section__chevron"
        icon="mdi-chevron-right"
        size="18"
      />

      <slot name="prepend-toolbar-title" />

      <span class="cc-filter-section__title" :title="title">
        {{ title }}
      </span>

      <span class="cc-filter-section__meta">
        <slot name="prepend-selected" />
        <slot name="append-toolbar-title" />
      </span>

      <span
        class="cc-filter-section__actions"
        @click.stop
        @keydown.stop
      >
        <slot name="prepend-toolbar-items" />

        <v-btn
          class="clear-btn"
          icon="mdi-close-circle-outline"
          size="small"
          variant="plain"
          title="Clear this filter"
          @click="emit('clear')"
        />

        <slot name="append-toolbar-items" />
      </span>
    </div>

    <div v-show="panelOpen" class="cc-filter-section__body">
      <slot />
    </div>
  </section>
</template>

<script setup>
// Web UI v2: replaces Filters/Layout/FilterToolbar.vue of src/ (see
// vite.config.mjs). A filter of the sidebar: a row with a chevron, the
// title, the selected values' summary and, on hover, the filter's buttons;
// below it the selected values. A mark at the left shows the filters that
// have values. The props, slots and events are the original ones.
//
// The content opens at once and fades in. An animated height would lay out
// the page in every frame of the animation, which makes opening lag.

import { ref, watch } from "vue";

const props = defineProps({
  title: { type: String, required: true },
  panelActive: { type: Boolean, default: false }
});

const emit = defineEmits([ "clear" ]);

// The content stays rendered while closed, as with the original `eager`
// expansion panel.
const panelOpen = ref(false);

watch(() => props.panelActive, active => {
  panelOpen.value = active;
}, { immediate: true });
</script>

<style lang="scss">
// Not scoped: the slots of each filter put their own elements in the row.

@keyframes cc-filter-open {
  from {
    opacity: 0;
    transform: translateY(-4px);
  }
  to {
    opacity: 1;
    transform: none;
  }
}

.cc-filter-section {
  position: relative;
  border-radius: var(--cc-radius-sm);
  transition: background-color .2s ease;

  &--open {
    background: rgba(var(--v-theme-on-surface), .025);
  }
}

.cc-filter-section__head {
  display: flex;
  align-items: center;
  gap: 6px;
  min-height: 40px;
  padding: 0 4px 0 6px;
  border-radius: inherit;
  cursor: pointer;
  user-select: none;
  outline: none;
  transition: background-color .2s ease;

  &:hover {
    background: rgba(var(--v-theme-primary), .05);
  }

  &:focus-visible {
    box-shadow: inset 0 0 0 2px rgba(var(--v-theme-primary), .45);
  }
}

.cc-filter-section__chevron {
  flex: 0 0 auto;
  color: var(--cc-muted);
  transition: transform .15s var(--cc-ease-out), color .2s ease;

  .cc-filter-section--open > .cc-filter-section__head > & {
    transform: rotate(90deg);
  }
}

// The title keeps the room for its first words; a long value shrinks first.
.cc-filter-section__title {
  flex: 1 1 auto;
  min-width: 45%;
  overflow: hidden;
  font-size: .85rem;
  font-weight: 600;
  white-space: nowrap;
  text-overflow: ellipsis;
  transition: color .2s ease;
}

.cc-filter-section__meta {
  display: inline-flex;
  flex: 0 1 auto;
  align-items: center;
  gap: 4px;
  min-width: 0;

  // A value shown in the row, such as a date range or a hash.
  .selected-items {
    max-width: 140px;
    overflow: hidden;
    font-size: .75rem;
    font-weight: 600;
    white-space: nowrap;
    text-overflow: ellipsis;
    color: rgb(var(--v-theme-primary));
  }

  .v-chip {
    height: 20px;
    padding: 0 6px;
    border-color: var(--cc-border);
  }
}

// A filter with values: the title takes the primary color, and a short
// bar stands before the chevron.
.cc-filter-section:has(
  > .cc-filter-section__head .cc-filter-badge,
  > .cc-filter-section__head .selected-items
) {
  > .cc-filter-section__head {
    .cc-filter-section__title,
    .cc-filter-section__chevron {
      color: rgb(var(--v-theme-primary));
    }

    &::before {
      content: "";
      position: absolute;
      top: 14px;
      left: 0;
      width: 3px;
      height: 12px;
      border-radius: 3px;
      background: var(--cc-gradient);
      animation: cc-fade .25s ease both;
    }
  }
}

// The filter's buttons (clear, settings, ...), shown on hover and while
// one of their menus is open.
.cc-filter-section__actions {
  display: none;
  flex: 0 0 auto;
  align-items: center;

  .v-btn {
    width: 28px !important;
    height: 28px !important;
    color: var(--cc-muted);
    opacity: 1;

    &:hover {
      color: rgb(var(--v-theme-primary));
    }

    .v-icon {
      font-size: 18px;
    }
  }

  .clear-btn:hover {
    color: rgb(var(--v-theme-error));
  }

  .cc-filter-section__head:hover > &,
  .cc-filter-section__head:focus-within > &,
  &:has([aria-expanded="true"]) {
    display: inline-flex;
    animation: cc-fade .2s ease both;
  }

  @media (hover: none) {
    display: inline-flex;
  }
}

.cc-filter-section__body {
  padding: 2px 8px 10px 30px;

  // Only opacity and transform: no layout while it plays.
  .cc-filter-section--open > & {
    animation: cc-filter-open .18s var(--cc-ease-out) both;
  }

  // The date and number inputs of some filters.
  > .v-container,
  > form > .v-container {
    padding: 0;
  }

  > .v-card-actions {
    min-height: 0;
    padding: 4px 0;
  }
}
</style>
