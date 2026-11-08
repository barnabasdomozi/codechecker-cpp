<template>
  <v-card class="cc-filter-menu" elevation="0">
    <slot name="prepend-toolbar" />

    <div v-if="search" class="cc-filter-menu__search">
      <v-text-field
        ref="search"
        v-model="searchTxt"
        autofocus
        hide-details
        color="primary"
        density="compact"
        variant="outlined"
        prepend-inner-icon="mdi-magnify"
        :placeholder="search.placeHolder"
        @update:model-value="filter"
      />
    </div>

    <slot name="append-toolbar" />

    <v-list
      class="cc-filter-menu__list overflow-y-auto"
      density="compact"
      :max-height="300"
    >
      <v-list-item
        v-if="searchTxt && search.regexLabel"
        class="regex-label"
        :value="searchTxt"
        @click="selectedRgx = selectedRgx === searchTxt ? null : searchTxt"
      >
        <template v-slot:prepend>
          <v-checkbox-btn
            :model-value="selectedRgx === searchTxt"
            color="primary"
            density="compact"
          />
        </template>

        <v-list-item-title>
          {{ search.regexLabel }}: <b>{{ searchTxt }}</b>
        </v-list-item-title>
      </v-list-item>

      <template v-if="items.length">
        <v-list-item
          v-for="item in processedItems"
          :key="item.id"
          :value="item.id"
          :active="false"
          :disabled="!multiple && isSelected(item.id)"
          :class="{ 'cc-filter-menu__item--selected': isSelected(item.id) }"
          @click="toggleSelection(item)"
        >
          <template v-slot:prepend>
            <v-checkbox-btn
              :model-value="isSelected(item.id)"
              color="primary"
              density="compact"
            />
            <slot name="icon" :item="item" />
          </template>

          <slot name="title" :item="item">
            <v-list-item-title :title="item.title">
              {{ item.title }}
            </v-list-item-title>
          </slot>

          <template v-slot:append>
            <slot
              name="prepend-count"
              :item="item"
            />

            <span
              v-if="item.count !== undefined"
              class="cc-filter-menu__count"
            >
              {{ formatCount(item.count) }}
            </span>
          </template>
        </v-list-item>
      </template>

      <template v-else>
        <slot name="no-items">
          <div class="cc-filter-menu__empty">
            <v-icon icon="mdi-text-search-variant" size="28" />
            No items
          </div>
        </slot>
      </template>
    </v-list>

    <div
      v-if="limit"
      class="cc-filter-menu__limit"
    >
      <span v-if="limit === items.length">Only the first </span>
      <b>{{ items.length }}</b> item(s) shown.
    </div>

    <div class="cc-filter-menu__actions">
      <span class="cc-filter-menu__selected">
        {{ localSelectedItems.length }} selected
      </span>

      <v-btn
        variant="text"
        class="cancel-btn"
        height="34"
        @click="$emit('cancel')"
      >
        Cancel
      </v-btn>

      <v-btn
        variant="flat"
        class="apply-btn"
        color="primary"
        height="34"
        prepend-icon="mdi-check"
        @click="apply"
      >
        Apply
      </v-btn>
    </div>
  </v-card>
</template>

<script setup>
// Web UI v2: replaces SelectOption/Items.vue of src/ (see vite.config.mjs).
// The menu of a filter's values: a search field, the values with check
// boxes and report counts, and Apply. The logic is the original one.

import _ from "lodash";
import { computed, onMounted, ref } from "vue";

const props = defineProps({
  items: { type: Array, required: true },
  format: { type: Function, default: null },
  limit: { type: Number, default: null },
  selectedItems: { type: Array, required: true },
  multiple: { type: Boolean, default: true },
  search: { type: Object, default: null },
});

const emit = defineEmits([
  "cancel",
  "update:items",
  "apply",
  "apply:finished"
]);

const searchTxt = ref(null);

const processedItems = computed(() => {
  if (!props.format) {
    return props.items;
  };
  return props.items.map(i => props.format(i));
});

const localSelectedItems = ref([]);

const selectedRgx = computed({
  get: () => {
    return props.selectedItems.find(item => item.id === searchTxt.value)
      ? searchTxt.value
      : null;
  },
  set: value => {
    const idx = localSelectedItems.value.findIndex(
      item => item.id === searchTxt.value
    );

    if (!value && idx !== -1) {
      localSelectedItems.value.splice(idx, 1);
    } else if (value && idx === -1) {
      localSelectedItems.value.push({ id: value, title: value });
    }
  }
});

const filter = _.debounce(async value => {
  const items = await props.search.filterItems(value);
  emit("update:items", items);
}, 500);

onMounted(() => {
  localSelectedItems.value = props.selectedItems
    ? [ ...props.selectedItems ]
    : [];
});

function formatCount(count) {
  return typeof count === "number" ? count.toLocaleString() : count;
}

// Emit the `apply` event with the selected items.
function apply() {
  emit("apply", localSelectedItems.value);
  emit("apply:finished");
}

// Check if given item is selected.
function isSelected(id) {
  return localSelectedItems.value.some(item => item.id === id);
}

// Handle clicking on any of the items in the list.
function toggleSelection(_item) {
  if (props.multiple) {
    // This component supports selecting multiple options.
    if (localSelectedItems.value.some(itm => itm.id === _item.id)) {
      localSelectedItems.value =
        localSelectedItems.value.filter(itm => itm.id !== _item.id);
    } else {
      localSelectedItems.value = [ ...localSelectedItems.value, _item ];
    }
  } else {
    // This component supports selecting only one option.
    localSelectedItems.value = [ _item ];
  }
}
</script>

<style lang="scss" scoped>
.cc-filter-menu {
  min-width: 320px;
  border: none !important;
}

.cc-filter-menu__search {
  padding: 12px 12px 4px;

  :deep(.v-field) {
    font-size: .85rem;
  }
}

.cc-filter-menu__list {
  padding: 6px;

  :deep(.v-list-item) {
    min-height: 36px !important;
    margin: 1px 0;
    padding: 0 8px !important;
    border-radius: var(--cc-radius-sm);
    font-size: .85rem;
  }

  :deep(.v-list-item__prepend) {
    gap: 4px;

    > .v-icon {
      font-size: 18px;
    }
  }

  :deep(.v-list-item__spacer) {
    width: 8px !important;
  }

  :deep(.v-list-item-title) {
    font-size: inherit;
  }

  // A value that will stay disabled: a single select's current value.
  :deep(.v-list-item--disabled) {
    opacity: .6;
  }
}

.cc-filter-menu__item--selected {
  background: rgba(var(--v-theme-primary), .06);
}

// The search text as a wildcard pattern.
.regex-label {
  margin-bottom: 4px !important;
  background: rgba(var(--v-theme-primary), .08);
  color: rgb(var(--v-theme-primary));
}

.cc-filter-menu__count {
  margin-left: 8px;
  padding: 1px 7px;
  border-radius: 999px;
  background: rgba(var(--v-theme-on-surface), .06);
  color: var(--cc-muted);
  font-size: .72rem;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.cc-filter-menu__empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 20px 12px;
  color: var(--cc-muted);
  font-size: .85rem;
}

.cc-filter-menu__limit {
  padding: 4px 16px 0;
  color: var(--cc-muted);
  font-size: .75rem;
  text-align: center;
}

.cc-filter-menu__actions {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 12px;
  border-top: 1px solid var(--cc-border);
}

.cc-filter-menu__selected {
  flex: 1 1 auto;
  color: var(--cc-muted);
  font-size: .78rem;
  font-variant-numeric: tabular-nums;
}

.cancel-btn {
  color: var(--cc-muted);
}
</style>
