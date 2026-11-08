<template>
  <div class="cc-rs-wrapper">
    <v-select
      :model-value="value.status"
      :items="items"
      :hide-details="true"
      :menu-props="{ contentClass: 'select-review-status-menu' }"
      :disabled="isReviewStatusDisabled"
      item-title="label"
      item-value="id"
      class="select-review-status cc-rs-select"
      density="compact"
      variant="solo"
      flat
      title="Review status"
      @update:model-value="onReviewStatusChange"
    >
      <template v-slot:selection="{ item }">
        <span class="cc-rs-select__value">
          <review-status-icon :status="item.value" size="16" />
          {{ item.title }}
        </span>
      </template>

      <template v-slot:item="{ item, props: itemProps }">
        <v-list-item v-bind="itemProps">
          <template v-slot:prepend>
            <review-status-icon :status="item.raw.id" size="16" />
          </template>
        </v-list-item>
      </template>
    </v-select>

    <v-dialog
      v-model="dialog"
      persistent
      max-width="560px"
      content-class="select-review-status-dialog"
    >
      <v-card class="cc-rs">
        <header class="cc-rs__header">
          <span class="cc-rs__header-icon">
            <review-status-icon :status="newReviewStatus" size="20" />
          </span>
          <div class="cc-rs__header-text">
            <div class="cc-rs__eyebrow">
              Change review status
            </div>
            <div class="cc-rs__title">
              <span class="cc-rs__old">{{ statusName(oldReviewStatus) }}</span>
              <v-icon icon="mdi-arrow-right" size="16" />
              {{ statusName(newReviewStatus) }}
            </div>
          </div>
          <v-btn
            class="cc-rs__close"
            icon="mdi-close"
            size="small"
            variant="text"
            title="Cancel"
            @click="cancelReviewStatusChange"
          />
        </header>

        <div class="cc-rs__body">
          <div v-if="isClosing" class="cc-rs__note">
            <v-icon icon="mdi-information-outline" size="18" />
            <div>
              False positive and intentional reports count as closed. The
              report is closed from now on.
            </div>
          </div>

          <div
            v-if="reportsReviewedInSource.length"
            class="cc-rs__note cc-rs__note--warning"
          >
            <v-icon icon="mdi-alert-outline" size="18" />
            <div>
              <div class="cc-rs__note-title">
                Reviewed in the source code
              </div>
              These reports get their review status from a source code
              comment or a review status rule file, which does not change.
              The status set here applies to every other report of this bug,
              also in the future, so the bug may get two different review
              statuses. We do not recommend it.

              <ul class="cc-rs__reports">
                <li
                  v-for="r in reportsReviewedInSource"
                  :key="parseInt(r.reportId)"
                >
                  <detection-status-icon
                    :status="r.detectionStatus"
                    :size="14"
                  />
                  <review-status-icon
                    :status="r.reviewData.status"
                    size="14"
                  />
                  <router-link
                    :to="{ name: 'report-detail', query: {
                      ...router.currentRoute.value.query,
                      'report-id': r.reportId,
                      'report-hash': undefined
                    }}"
                    target="_blank"
                  >
                    {{ r.$runName }}:
                    {{ r.checkedFile.replace(/^.*[\\/]/, "") }}:L{{ r.line }}
                  </router-link>
                  <span
                    v-if="parseInt(r.reportId) === parseInt(report.reportId)"
                    class="cc-rs__current"
                  >
                    current report
                  </span>
                </li>
              </ul>
            </div>
          </div>

          <v-textarea
            v-model="reviewStatusMessage"
            class="cc-rs__comment"
            name="reviewStatusMessage"
            label="Comment (optional)"
            placeholder="Why does the status change?"
            variant="outlined"
            color="primary"
            density="compact"
            rows="3"
            auto-grow
            autofocus
            :hide-details="true"
          />
        </div>

        <footer class="cc-rs__footer">
          <v-btn
            class="cancel-btn"
            variant="text"
            size="small"
            @click="cancelReviewStatusChange"
          >
            Cancel
          </v-btn>
          <v-btn
            class="confirm-btn"
            color="primary"
            variant="flat"
            size="small"
            @click="confirmReviewStatusChange"
          >
            Change status
          </v-btn>
        </footer>
      </v-card>
    </v-dialog>
  </div>
</template>

<script setup>
// Web UI v2: replaces src/components/Report/SelectReviewStatusDialog.vue
// (see vite.config.mjs). The review status of the report page: a select as
// small as the buttons beside it, and the dialog that confirms a change.
// The props, the events and the logic are the original ones, except that
// the note about closing reports follows the new status: the report page
// does not apply update:value, so the original showed it for the old one.

import { DetectionStatusIcon, ReviewStatusIcon } from "@/components/Icons";
import { useReviewStatus } from "@/composables/useReviewStatus";
import { ccService } from "@cc-api";
import { ReviewStatus } from "@cc/report-server-types";
import { computed, ref } from "vue";
import { useRouter } from "vue-router";
import { useStore } from "vuex";

const props = defineProps({
  value: { type: Object, default: () => {} },
  report: { type: Object, default: () => {} },
  onConfirm: { type: Function, default: () => {} }
});

const emit = defineEmits([ "update:value" ]);

const reviewStatus = useReviewStatus();

const dialog = ref(false);
const oldReviewStatus = ref(0);
const newReviewStatus = ref(0);
const reviewStatusMessage = ref(null);
const sameReports = ref(null);
const store = useStore();
const router = useRouter();

const items = Object.values(ReviewStatus).map(_id => ({
  id: _id,
  label: reviewStatus.reviewStatusFromCodeToString(parseInt(_id))
}));

const statusName = status =>
  reviewStatus.reviewStatusFromCodeToString(parseInt(status));

const currentProductConfig = computed(
  () => store.getters.currentProductConfig
);
const currentUser = computed(() => store.getters.currentUser);

const isReviewStatusDisabled = computed(() => {
  if (!currentProductConfig.value) return true;
  return currentProductConfig.value.isReviewStatusChangeDisabled;
});

const reportsReviewedInSource = computed(() => {
  if (!sameReports.value) return [];
  return sameReports.value.filter(report => report.reviewData.isInSource);
});

const isClosing = computed(() =>
  newReviewStatus.value === ReviewStatus.FALSE_POSITIVE ||
  newReviewStatus.value === ReviewStatus.INTENTIONAL
);

async function onReviewStatusChange(_value) {
  oldReviewStatus.value = props.value.status;
  emit("update:value", { ...props.value, status: _value });
  reviewStatusMessage.value = props.value.message;
  newReviewStatus.value = _value;
  sameReports.value = null;

  dialog.value = true;

  sameReports.value = await ccService.getSameReports(props.report.bugHash);
}

function confirmReviewStatusChange() {
  const _comment = reviewStatusMessage.value || "";
  const _author = currentUser.value || "Anonymous";
  props.onConfirm(_comment, newReviewStatus.value, _author);
  dialog.value = false;
}

function cancelReviewStatusChange() {
  emit("update:value", { ...props.value, status: oldReviewStatus.value });
  dialog.value = false;
}
</script>

<style lang="scss" scoped>
// A block: an inline box would leave room for text descenders under the
// select, which then stands higher than the buttons beside it.
.cc-rs-wrapper {
  display: flex;
}

// The select: as high as the small buttons of the toolbar (28px).
.cc-rs-select {
  min-width: 158px;

  :deep(.v-field) {
    min-height: 28px !important;
    height: 28px;
    padding: 0 4px 0 10px;
    border: 1px solid var(--cc-border);
    border-radius: var(--cc-radius-sm);
    background: rgb(var(--v-theme-surface));
    font-size: .8rem;
    transition: border-color .2s ease, box-shadow .25s ease;
  }

  :deep(.v-field:hover) {
    border-color: rgba(var(--v-theme-primary), .35);
  }

  :deep(.v-field--focused) {
    border-color: rgba(var(--v-theme-primary), .5);
  }

  :deep(.v-field__field),
  :deep(.v-field__input) {
    min-height: 28px !important;
    max-height: 28px;
    padding: 0;
  }

  :deep(.v-field__input input) {
    display: none;
  }

  :deep(.v-field__append-inner) {
    align-items: center;
    padding: 0;

    .v-icon {
      font-size: 18px;
      color: var(--cc-muted);
    }
  }
}

.cc-rs-select__value {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-weight: 600;
  white-space: nowrap;
}

// The dialog.
.cc-rs__header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 16px 14px 20px;
  border-bottom: 1px solid var(--cc-border);
}

.cc-rs__header-icon {
  display: flex;
  flex: 0 0 auto;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: rgba(var(--v-theme-on-surface), .05);
}

.cc-rs__header-text {
  flex: 1 1 auto;
  min-width: 0;
}

.cc-rs__eyebrow {
  color: var(--cc-muted);
  font-size: .75rem;
  font-weight: 600;
}

.cc-rs__title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 1.05rem;
  font-weight: 650;
  line-height: 1.3;
  letter-spacing: -.01em;

  .v-icon {
    color: var(--cc-muted);
  }
}

.cc-rs__old {
  color: var(--cc-muted);
  font-weight: 550;
}

.cc-rs__close {
  color: var(--cc-muted);
}

.cc-rs__body {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 16px 20px;
}

.cc-rs__note {
  display: flex;
  gap: 10px;
  padding: 10px 12px;
  border: 1px solid rgba(var(--v-theme-info), .3);
  border-radius: var(--cc-radius);
  background: rgba(var(--v-theme-info), .07);
  font-size: .82rem;
  line-height: 1.5;

  > .v-icon {
    flex: 0 0 auto;
    margin-top: 1px;
    color: rgb(var(--v-theme-info));
  }

  &--warning {
    border-color: rgba(var(--v-theme-warning), .35);
    background: rgba(var(--v-theme-warning), .08);

    > .v-icon {
      color: rgb(var(--v-theme-warning));
    }
  }
}

.cc-rs__note-title {
  margin-bottom: 2px;
  font-weight: 650;
}

.cc-rs__reports {
  margin: 8px 0 0;
  padding: 0;
  list-style: none;

  li {
    display: flex;
    align-items: center;
    gap: 4px;
    padding: 2px 0;
  }

  a {
    margin-left: 2px;
    color: rgb(var(--v-theme-primary));
    font-family: var(--cc-font-mono);
    font-size: .78rem;
    text-decoration: none;

    &:hover {
      text-decoration: underline;
      text-underline-offset: 3px;
    }
  }
}

.cc-rs__current {
  padding: 0 6px;
  border-radius: 999px;
  background: rgba(var(--v-theme-primary), .1);
  color: rgb(var(--v-theme-primary));
  font-size: .68rem;
  font-weight: 650;
  line-height: 16px;
}

.cc-rs__comment :deep(textarea) {
  font-size: .85rem;
}

.cc-rs__footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  padding: 12px 16px;
  border-top: 1px solid var(--cc-border);

  .cancel-btn {
    color: var(--cc-muted);
  }
}
</style>

<style lang="scss">
// The menu of the select, which Vuetify moves to the end of <body>.
.select-review-status-menu .v-list {
  padding: 4px;

  .v-list-item {
    min-height: 32px !important;
    padding-inline: 8px !important;
    border-radius: var(--cc-radius-sm);
  }

  .v-list-item__prepend > .v-icon {
    margin-inline-end: 0;
  }

  .v-list-item__spacer {
    width: 8px !important;
  }

  .v-list-item-title {
    font-size: .82rem;
    font-weight: 550;
  }

  // The current status: the primary tint instead of a gray box.
  .v-list-item--active {
    color: rgb(var(--v-theme-primary));

    > .v-list-item__overlay {
      background: rgb(var(--v-theme-primary));
      opacity: .08;
    }
  }

  .v-list-item:focus-visible > .v-list-item__overlay {
    opacity: .1;
  }
}
</style>
