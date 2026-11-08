<template>
  <div ref="rootRef" class="cc-preset">
    <label :for="chooserId" class="cc-preset__label">
      Filter preset
    </label>
    <v-menu
      v-model="menuOpen"
      location="bottom start"
      offset="6"
      :close-on-content-click="false"
      :disabled="busy"
    >
      <template v-slot:activator="{ props: menuProps }">
        <button
          v-bind="menuProps"
          :id="chooserId"
          type="button"
          class="cc-preset__chooser"
          :class="{ 'cc-preset__chooser--active': hasPreset }"
          :disabled="busy"
          :title="hasPreset
            ? `Filter preset: ${activePresetName}`
            : 'Choose or save a filter preset'"
        >
          <v-icon
            class="cc-preset__icon"
            :icon="hasPreset ? 'mdi-bookmark' : 'mdi-bookmark-outline'"
            size="18"
          />
          <span v-if="hasPreset" class="cc-preset__name">
            {{ activePresetName || "Preset" }}
          </span>
          <span v-else class="cc-preset__placeholder">
            No preset
          </span>
          <span
            v-if="isModified"
            class="cc-preset__modified"
            title="The filters differ from the saved preset"
          >
            Edited
          </span>
          <v-progress-circular
            v-if="busy"
            class="cc-preset__chevron"
            indeterminate
            size="14"
            width="2"
          />
          <v-icon
            v-else
            class="cc-preset__chevron"
            icon="mdi-chevron-down"
            size="18"
          />
        </button>
      </template>

      <v-card class="cc-preset-menu" elevation="0">
        <div class="cc-preset-menu__head">
          Filter presets
          <span class="cc-preset-menu__hint">
            Saved filters of this product
          </span>
        </div>

        <v-progress-linear
          v-if="loadingList"
          indeterminate
          height="2"
        />

        <v-list
          class="cc-preset-menu__list"
          density="compact"
          max-height="260"
        >
          <v-list-item
            v-for="preset in presets"
            :key="preset.id"
            :active="false"
            :class="{
              'cc-preset-menu__item--active': preset.id === activePresetId
            }"
            :title="preset.name"
            @click="selectPreset(preset.id)"
          >
            <template v-slot:prepend>
              <v-icon
                class="cc-preset-menu__check"
                icon="mdi-check"
                size="16"
              />
            </template>

            <template v-if="canSeeActions" v-slot:append>
              <v-btn
                class="cc-preset-menu__delete"
                size="x-small"
                variant="text"
                :loading="deletingId === preset.id"
                :title="`Delete '${preset.name}'`"
                icon="mdi-trash-can-outline"
                @click.stop="promptDelete(preset.id)"
              />
            </template>
          </v-list-item>

          <div
            v-if="!loadingList && !presets.length"
            class="cc-preset-menu__empty"
          >
            {{ error || "No saved presets yet." }}
          </div>
        </v-list>

        <template v-if="actions.length">
          <v-divider />

          <v-list class="cc-preset-menu__actions" density="compact">
            <v-list-item
              v-for="action in actions"
              :key="action.id"
              :prepend-icon="action.icon"
              :title="action.title"
              :subtitle="action.subtitle"
              :class="`cc-preset-menu__action--${action.id}`"
              @click="action.run"
            />
          </v-list>
        </template>
      </v-card>
    </v-menu>

    <ConfirmDialog
      v-model="showDeleteConfirmDialog"
      max-width="400px"
      cancel-btn-color="primary"
      confirm-btn-label="Delete"
      confirm-btn-color="error"
      title="Delete Preset"
      @confirm="confirmDelete"
    >
      <template v-slot:content>
        Delete "<b>{{ pendingDeleteName }}</b>"? This action cannot be undone.
      </template>
    </ConfirmDialog>
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/ReportFilter/Filters/PresetMenu.vue
// (see vite.config.mjs). The filter preset of the filter sidebar: a label
// and one row with the active preset (or "No preset"), which opens a menu
// with the saved presets and the actions to save, rename or override one.
//
// The events, the exposed functions and the preset logic are the original
// ones. The original sidebar (src/.../ReportFilter.vue) keeps its preset
// buttons and the save dialog behind them; v2 hides those buttons (see
// src-v2/.../ReportFilter.vue), and the actions of this menu click them.

import { computed, nextTick, onMounted, ref, useId, watch } from "vue";
import isEqual from "lodash/isEqual";
import ConfirmDialog from "@/components/ConfirmDialog";
import {
  authService,
  ccService,
  handleThriftError,
  prodService } from "@cc-api";
import { Permission } from "@cc/shared-types";
import { useRoute } from "vue-router";

const emit = defineEmits([ "apply-preset", "clear-preset" ]);

const rootRef = ref(null);
const chooserId = `cc-preset-chooser-${useId()}`;
const menuOpen = ref(false);
const presets = ref([]);
const loadingList = ref(false);
const settingPreset = ref(false);
const deletingId = ref(null);
const saving = ref(false);
const error = ref(null);
const isSuperUser = ref(false);
const isAdminOfAnyProduct = ref(false);
const activePresetId = ref(null);
const querySnapshot = ref(null);
const isModified = ref(false);
const showDeleteConfirmDialog = ref(false);
const pendingDeleteId = ref(null);

const pendingDeleteName = computed(() => {
  const preset = presets.value.find(p => p.id === pendingDeleteId.value);
  return preset?.name || "";
});

const route = useRoute();

const canSeeActions = computed(() =>
  isSuperUser.value || isAdminOfAnyProduct.value
);

const activePresetName = computed(() => {
  const preset =
    presets.value.find(preset => preset.id === activePresetId.value);
  return preset?.name || "";
});

const hasPreset = computed(() => activePresetId.value != null);

const busy = computed(() =>
  saving.value || settingPreset.value || deletingId.value !== null
);

// The buttons of the original sidebar, in the order it shows them in each
// state: "Create Preset" without a preset; "Rename" with an unchanged
// preset; "Override Preset" and "Create as new" with a changed one.
const actions = computed(() => {
  const list = [];

  if (canSeeActions.value) {
    if (!hasPreset.value && !isModified.value) {
      list.push({
        id: "create",
        icon: "mdi-bookmark-plus-outline",
        title: "New preset",
        subtitle: "From the current filters",
        run: () => clickOriginalButton(0)
      });
    } else if (hasPreset.value && !isModified.value) {
      list.push({
        id: "rename",
        icon: "mdi-pencil-outline",
        title: "Rename preset",
        subtitle: "Change the name of the preset",
        run: () => clickOriginalButton(0)
      });
    } else if (isModified.value) {
      list.push({
        id: "override",
        icon: "mdi-content-save-outline",
        title: "Update preset",
        subtitle: "Save the current filters to it",
        run: () => clickOriginalButton(0)
      }, {
        id: "create-new",
        icon: "mdi-bookmark-plus-outline",
        title: "Save as new preset",
        subtitle: "Keep the preset unchanged",
        run: () => clickOriginalButton(1)
      });
    }
  }

  if (hasPreset.value && isModified.value) {
    list.push({
      id: "revert",
      icon: "mdi-restore",
      title: "Discard changes",
      subtitle: "Go back to the saved filters",
      run: () => selectPreset(activePresetId.value)
    });
  }

  if (hasPreset.value) {
    list.push({
      id: "clear",
      icon: "mdi-bookmark-remove-outline",
      title: "Clear preset",
      subtitle: "Remove the preset and all filters",
      run: () => {
        menuOpen.value = false;
        handleClear();
      }
    });
  }

  return list;
});

function clickOriginalButton(index) {
  menuOpen.value = false;
  const buttons = rootRef.value?.parentElement?.querySelectorAll(
    ":scope > div.d-flex > .v-btn");
  buttons?.[index]?.click();
}

function checkModified(currentQuery) {
  if (!activePresetId.value || !querySnapshot.value) {
    isModified.value = false;
    return;
  }

  const normalize = query => {
    const sorted = {};
    for (const key of Object.keys(query).sort()) {
      const value = query[key];
      if (value === undefined || value === null || value === ""
        || value === "off") continue;
      if (Array.isArray(value)) {
        const filtered = value
          .filter(x => x !== undefined && x !== null && x !== ""
            && x !== "off")
          .map(String)
          .sort();
        if (filtered.length) {
          sorted[key] = filtered.length === 1 ? filtered[0] : filtered;
        }
      } else {
        sorted[key] = String(value);
      }
    }
    return sorted;
  };
  const normNew = normalize(currentQuery);
  const normSnap = normalize(querySnapshot.value);

  isModified.value = !isEqual(normNew, normSnap);
}

watch(() => route.query, newQuery => {
  checkModified(newQuery);
}, { deep: true });

watch(activePresetId, newVal => {
  if (newVal == null || settingPreset.value) return;
  settingPreset.value = true;
  emit("apply-preset", newVal);
  settingPreset.value = false;
});

watch(menuOpen, open => {
  if (open) fetchPresets();
});

onMounted(() => {
  authService.getClient().hasPermission(
    Permission.SUPERUSER,
    "",
    handleThriftError(_isSuperUser => {
      isSuperUser.value = _isSuperUser;

      if (!isSuperUser.value) {
        prodService.getClient().isAdministratorOfAnyProduct(
          handleThriftError(isAdmin => {
            isAdminOfAnyProduct.value = isAdmin;
          })
        );
      }
    })
  );
});

// Choosing the active preset again applies its saved filters again.
function selectPreset(id) {
  menuOpen.value = false;
  if (id === activePresetId.value) {
    if (isModified.value) emit("apply-preset", id);
    return;
  }
  activePresetId.value = id;
}

async function fetchPresets() {
  error.value = null;
  loadingList.value = true;

  try {
    const res = await new Promise((resolve, reject) => {
      ccService.getClient().listFilterPreset((err, presetList) => {
        if (err) return reject(err);
        resolve(presetList);
      });
    });

    if (!res) {
      presets.value = [];
      return;
    }

    presets.value = (Array.isArray(res) ? res : []).map(p => ({
      id: p.id.toNumber(),
      name: p.name,
    }));

  } catch (e) {
    error.value = (e && e.message) ? e.message : "Failed to load presets";
    presets.value = [];
  } finally {
    loadingList.value = false;
  }
}

function onPresetApplied(savedQuery) {
  querySnapshot.value = savedQuery ? { ...savedQuery } : { ...route.query };
  checkModified(route.query);
}

function clearPresetState() {
  activePresetId.value = null;
  querySnapshot.value = null;
  isModified.value = false;
}

function promptDelete(id) {
  pendingDeleteId.value = id;
  showDeleteConfirmDialog.value = true;
}

async function confirmDelete() {
  showDeleteConfirmDialog.value = false;
  if (pendingDeleteId.value !== null) {
    await deletePreset(pendingDeleteId.value);
    pendingDeleteId.value = null;
  }
}

async function deletePreset(id) {
  error.value = null;
  deletingId.value = id;

  try {
    await new Promise((resolve, reject) => {
      ccService.getClient().deleteFilterPreset(id, err => {
        if (err) return reject(err);
        resolve();
      });
    });

    presets.value = presets.value.filter(p => p.id !== id);

    if (activePresetId.value === id) {
      clearPresetState();
    }
  } catch (e) {
    error.value = (e && e.message) ? e.message : "Failed to delete preset";
  } finally {
    deletingId.value = null;
  }
}

function handleClear() {
  clearPresetState();
  emit("clear-preset");
}

async function selectPresetAfterSave(id) {
  const numId = typeof id?.toNumber === "function" ? id.toNumber() : id;
  await fetchPresets();
  activePresetId.value = numId;
  querySnapshot.value = { ...route.query };
  isModified.value = false;
}

async function selectPresetSilently(id) {
  settingPreset.value = true;
  activePresetId.value = id;
  await nextTick();
  settingPreset.value = false;
}

defineExpose({
  activePresetId,
  activePresetName,
  isModified,
  onPresetApplied,
  clearPresetState,
  selectPresetAfterSave,
  selectPresetSilently,
  fetchPresets,
});
</script>

<style lang="scss" scoped>
.cc-preset {
  flex: 1 1 auto;
  min-width: 0;
}

// The label above the chooser.
.cc-preset__label {
  display: block;
  margin: 0 0 6px 2px;
  color: var(--cc-muted);
  font-size: .75rem;
  font-weight: 600;
  line-height: 1.2;
}

// The chooser: one row the width of the sidebar.
.cc-preset__chooser {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  height: 36px;
  padding: 0 8px 0 10px;
  border: 1px solid var(--cc-border);
  border-radius: var(--cc-radius-sm);
  background: rgba(var(--v-theme-on-surface), .03);
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
  transition: background-color .2s ease, border-color .2s ease,
    box-shadow .25s ease;

  &:hover:not(:disabled) {
    border-color: rgba(var(--v-theme-primary), .35);
    background: rgba(var(--v-theme-primary), .05);
  }

  &:focus-visible,
  &[aria-expanded="true"] {
    outline: none;
    border-color: rgba(var(--v-theme-primary), .5);
    box-shadow: 0 0 0 4px rgba(29, 78, 216, .15);
  }

  &:disabled {
    cursor: progress;
  }
}

.cc-preset__icon {
  flex: 0 0 auto;
  color: var(--cc-muted);

  .cc-preset__chooser--active > & {
    color: rgb(var(--v-theme-primary));
  }
}

.cc-preset__name,
.cc-preset__placeholder {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  font-size: .85rem;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.cc-preset__name {
  font-weight: 600;
}

.cc-preset__placeholder {
  color: var(--cc-muted);
  font-weight: 500;
}

.cc-preset__modified {
  flex: 0 0 auto;
  padding: 1px 7px;
  border-radius: 999px;
  background: rgba(var(--v-theme-warning), .16);
  color: rgb(var(--v-theme-warning));
  font-size: .68rem;
  font-weight: 700;
  letter-spacing: .02em;
  animation: cc-fade .2s ease both;
}

.cc-preset__chevron {
  flex: 0 0 auto;
  color: var(--cc-muted);
  transition: transform .2s var(--cc-ease-out);

  [aria-expanded="true"] > & {
    transform: rotate(180deg);
  }
}

// The menu.
.cc-preset-menu {
  width: 300px;
  max-width: calc(100vw - 32px);
}

.cc-preset-menu__head {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 12px 14px 8px;
  font-size: .72rem;
  font-weight: 650;
  text-transform: uppercase;
  letter-spacing: .06em;
  color: var(--cc-muted);
}

.cc-preset-menu__hint {
  font-size: .75rem;
  font-weight: 450;
  text-transform: none;
  letter-spacing: 0;
}

.cc-preset-menu__list,
.cc-preset-menu__actions {
  padding: 4px 6px 6px;

  .v-list-item {
    min-height: 34px !important;
    border-radius: var(--cc-radius-sm);
  }
}

.cc-preset-menu__list {
  :deep(.v-list-item-title) {
    font-size: .85rem;
  }

  :deep(.v-list-item__prepend) {
    width: 24px;
  }
}

// The active preset: a check mark and the primary color.
.cc-preset-menu__check {
  opacity: 0;
  color: rgb(var(--v-theme-primary));
}

.cc-preset-menu__item--active {
  background: rgba(var(--v-theme-primary), .08);
  color: rgb(var(--v-theme-primary));

  :deep(.v-list-item-title) {
    font-weight: 600;
  }

  .cc-preset-menu__check {
    opacity: 1;
  }
}

// The delete button shows on hover.
.cc-preset-menu__delete {
  color: var(--cc-muted);
  opacity: 0;
  transition: opacity .15s ease, color .2s ease;

  .v-list-item:hover &,
  .v-list-item:focus-within &,
  &.v-btn--loading {
    opacity: 1;
  }

  &:hover {
    color: rgb(var(--v-theme-error));
  }

  @media (hover: none) {
    opacity: 1;
  }
}

.cc-preset-menu__empty {
  padding: 8px 10px 10px;
  color: var(--cc-muted);
  font-size: .82rem;
}

.cc-preset-menu__actions {
  .v-list-item {
    padding-block: 4px !important;
  }

  :deep(.v-list-item__prepend) {
    align-self: flex-start;
    padding-top: 2px;

    > .v-icon {
      color: rgb(var(--v-theme-primary));
      opacity: 1;
    }

    > .v-list-item__spacer {
      width: 12px !important;
    }
  }

  :deep(.v-list-item-title) {
    font-size: .85rem;
    font-weight: 600;
  }

  :deep(.v-list-item-subtitle) {
    font-size: .74rem;
    line-height: 1.35;
  }

  .cc-preset-menu__action--clear :deep(.v-list-item__prepend > .v-icon) {
    color: var(--cc-muted);
  }
}
</style>
