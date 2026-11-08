<template>
  <div v-if="localSelectedItems.length" class="cc-filter-values">
    <div
      v-for="item in localSelectedItems"
      :key="item.id"
      class="selected-item cc-filter-value"
      :class="{ 'cc-filter-value--removable': multiple }"
      :role="multiple ? 'button' : undefined"
      :tabindex="multiple ? 0 : undefined"
      :title="multiple ? 'Remove from the filter' : undefined"
      @click="multiple && removeSelected(item)"
      @keydown.enter.prevent="multiple && removeSelected(item)"
      @keydown.space.prevent="multiple && removeSelected(item)"
    >
      <span class="cc-filter-value__icon">
        <slot name="icon" :item="item" />
      </span>

      <span class="cc-filter-value__title">
        <slot name="title" :item="item">
          <span :title="item.title">{{ item.title }}</span>
        </slot>
      </span>

      <span class="cc-filter-value__count report-count">
        {{ formatCount(item.count) }}
      </span>

      <v-icon
        v-if="multiple"
        class="cc-filter-value__remove remove-btn"
        icon="mdi-close"
        size="16"
      />
    </div>
  </div>

  <div v-else class="cc-filter-values__empty">
    No filter
  </div>
</template>

<script setup>
// Web UI v2: replaces SelectOption/ItemsSelected.vue of src/ (see
// vite.config.mjs). The selected values of a filter, as soft chips with
// their report counts. A click on a value removes it; on hover the count
// turns into a cross. The props and events are the original ones.

import { onMounted, ref, watch } from "vue";

const props = defineProps({
  selectedItems: { type: Array, required: true },
  multiple: { type: Boolean, default: true }
});

const emit = defineEmits([ "update:select" ]);

const localSelectedItems = ref([]);

onMounted(() => {
  localSelectedItems.value = props.selectedItems;
});

watch(() => props.selectedItems, val => {
  localSelectedItems.value = Array.isArray(val) ? val : [ val ];
}, { deep: true });

function formatCount(count) {
  if (typeof count === "number") return count.toLocaleString();
  return count || count === 0 ? count : "N/A";
}

function removeSelected(_item) {
  if (localSelectedItems.value.some(itm => itm.id === _item.id)) {
    localSelectedItems.value =
      localSelectedItems.value.filter(itm => itm.id !== _item.id);
  }

  emit("update:select", localSelectedItems.value);
}
</script>

<style lang="scss" scoped>
.cc-filter-values {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.cc-filter-value {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 30px;
  padding: 3px 6px 3px 8px;
  border-radius: var(--cc-radius-sm);
  background: rgba(var(--v-theme-primary), .07);
  font-size: .8rem;
  transition: background-color .2s ease, color .2s ease;
  animation: cc-fade .25s ease both;

  &--removable {
    cursor: pointer;
    outline: none;

    &:hover,
    &:focus-visible {
      background: rgba(var(--v-theme-error), .1);

      .cc-filter-value__count {
        display: none;
      }

      .cc-filter-value__remove {
        display: inline-flex;
      }
    }

    &:focus-visible {
      box-shadow: inset 0 0 0 2px rgba(var(--v-theme-error), .4);
    }
  }
}

.cc-filter-value__icon {
  display: inline-flex;
  flex: 0 0 auto;
  align-items: center;

  &:empty {
    display: none;
  }

  :deep(.v-icon) {
    font-size: 18px;
  }
}

.cc-filter-value__title {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;

  :deep(.v-list-item-title) {
    overflow: hidden;
    font-size: inherit;
    line-height: 1.35;
    text-overflow: ellipsis;
  }
}

.cc-filter-value__count {
  flex: 0 0 auto;
  padding: 1px 7px;
  border-radius: 999px;
  background: rgba(var(--v-theme-on-surface), .06);
  color: var(--cc-muted);
  font-size: .72rem;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.cc-filter-value__remove {
  display: none;
  flex: 0 0 auto;
  color: rgb(var(--v-theme-error));
}

.cc-filter-values__empty {
  padding: 2px 0 4px;
  color: var(--cc-muted);
  font-size: .8rem;
}
</style>
