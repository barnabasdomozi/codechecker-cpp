<template>
  <v-card
    class="cc-product-card"
    :class="{
      'cc-product-card--usable': usable,
      'cc-product-card--disabled': !usable
    }"
    :style="{
      '--cc-accent': accentColor(product.endpoint),
      '--cc-accent-soft': accentColor(product.endpoint, .14),
      '--cc-accent-gradient': accentGradient(product.endpoint)
    }"
    :ripple="false"
    variant="flat"
    @click="openProduct"
  >
    <span class="cc-product-card__glow" />

    <div class="cc-product-card__header">
      <div class="cc-product-card__avatar">
        {{ product.endpoint.charAt(0).toUpperCase() }}
      </div>

      <div class="cc-product-card__titles">
        <div class="cc-product-card__name">
          <confidentiality-icon
            class="mr-1"
            :value="product.confidentiality"
            small
          />
          <span
            :class="{ 'text-decoration-line-through': !usable }"
            :title="product.displayedName"
          >
            {{ product.displayedName }}
          </span>
        </div>
        <div class="cc-product-card__endpoint">
          /{{ product.endpoint }}
        </div>
      </div>

      <div
        v-if="administrating || isSuperUser"
        class="cc-product-card__actions"
        @click.stop
      >
        <edit-product-btn
          v-if="administrating"
          :product="product"
          :is-super-user="isSuperUser"
          @on-complete="emit('changed')"
        />
        <delete-product-btn
          v-if="isSuperUser"
          :product="product"
          @on-complete="emit('changed')"
        />
      </div>
    </div>

    <p class="cc-product-card__description">
      {{ product.description || "No description." }}
    </p>

    <div
      v-if="problem"
      class="cc-product-card__problem"
    >
      <v-icon icon="mdi-alert-outline" size="18" />
      <span>{{ problem }}</span>
    </div>

    <div class="cc-product-card__stats">
      <div class="cc-product-card__stat">
        <v-icon icon="mdi-run-fast" size="16" />
        <strong>{{ product.runCount }}</strong>
        {{ product.runCount === 1 ? "run" : "runs" }}
      </div>
      <div
        v-if="product.latestStoreToProduct"
        class="cc-product-card__stat"
        :title="`Latest store: ${prettifyDate(product.latestStoreToProduct)}`"
      >
        <v-icon icon="mdi-cloud-upload-outline" size="16" />
        {{ latestStoreAgo }}
      </div>
    </div>

    <div class="cc-product-card__footer">
      <div class="cc-product-card__admins">
        <span
          v-for="(admin, i) in shownAdmins"
          :key="admin"
          class="cc-product-card__admin"
          :style="{ background: accentGradient(admin), zIndex: 10 - i }"
          :title="`Admin: ${admin}`"
        >
          {{ admin.charAt(0).toUpperCase() }}
        </span>
        <span
          v-if="product.admins.length > shownAdmins.length"
          class="cc-product-card__admin cc-product-card__admin--more"
          :title="product.admins.slice(shownAdmins.length).join(', ')"
        >
          +{{ product.admins.length - shownAdmins.length }}
        </span>
      </div>

      <div
        v-if="usable"
        class="cc-product-card__links"
        @click.stop
      >
        <v-btn
          v-for="link in links"
          :key="link.name"
          :to="{
            name: link.name,
            params: { endpoint: product.endpoint },
            query: link.query || {}
          }"
          :title="link.title"
          :icon="link.icon"
          size="small"
          variant="text"
          class="cc-product-card__link"
        />
      </div>
    </div>
  </v-card>
</template>

<script setup>
import { computed } from "vue";
import { useRouter } from "vue-router";
import { formatDistanceToNowStrict } from "date-fns";

import { ConfidentialityIcon } from "@/components/Icons";
import { DeleteProductBtn, EditProductBtn } from "@/components/Product";
import { defaultReportFilterValues } from "@/components/Report/ReportFilter";
import { useDateUtils } from "@/composables/useDateUtils";
import { DBStatus } from "@cc/shared-types";
import { accentColor, accentGradient } from "@v2/composables/useAccentColor";

const props = defineProps({
  product: { type: Object, required: true },
  isSuperUser: { type: Boolean, default: false }
});

const emit = defineEmits([ "changed" ]);

const router = useRouter();
const { prettifyDate, strToDateTime } = useDateUtils();

const MAX_SHOWN_ADMINS = 4;

const links = [
  { name: "runs", title: "Show runs", icon: "mdi-run-fast" },
  {
    name: "reports",
    title: "Show reports",
    icon: "mdi-bug-outline",
    query: defaultReportFilterValues
  },
  {
    name: "statistics",
    title: "Show statistics",
    icon: "mdi-chart-box-outline",
    query: defaultReportFilterValues
  }
];

const usable = computed(() =>
  props.product.accessible && props.product.databaseStatus === DBStatus.OK);

const administrating = computed(() => !!props.product.administrating);

const shownAdmins = computed(() =>
  props.product.admins.slice(0, MAX_SHOWN_ADMINS));

const latestStoreAgo = computed(() => {
  const text = prettifyDate(props.product.latestStoreToProduct);
  const date = strToDateTime(text);
  if (!date || isNaN(date)) return text;
  return `${formatDistanceToNowStrict(date)} ago`;
});

const problem = computed(() => {
  if (!props.product.accessible)
    return "You do not have access to this product.";

  switch (props.product.databaseStatus) {
  case DBStatus.OK:
    return null;
  case DBStatus.MISSING:
    return "Database is missing.";
  case DBStatus.FAILED_TO_CONNECT:
    return "Failed to connect to the database.";
  case DBStatus.SCHEMA_MISMATCH_OK:
    return "Schema mismatch: run 'CodeChecker server' to migrate.";
  case DBStatus.SCHEMA_MISMATCH_NO:
    return "Schema mismatch: migration not available.";
  case DBStatus.SCHEMA_MISSING:
    return "Schema is missing: run 'CodeChecker server' to initialize.";
  case DBStatus.SCHEMA_INIT_ERROR:
    return "Schema initialization error.";
  default:
    return "Unknown database status.";
  }
});

function openProduct() {
  if (!usable.value) return;
  router.push({ name: "runs", params: { endpoint: props.product.endpoint } });
}
</script>

<style lang="scss" scoped>
.cc-product-card {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 14px;
  height: 100%;
  padding: 20px;
  overflow: hidden;
  background: rgb(var(--v-theme-surface));
  border: 1px solid var(--cc-border) !important;
  box-shadow: var(--cc-shadow-sm) !important;
  transition: transform .35s var(--cc-ease-out), box-shadow .35s ease,
    border-color .35s ease !important;

  &--usable {
    cursor: pointer;

    &:hover {
      transform: translateY(-5px);
      box-shadow: var(--cc-shadow-lg) !important;
      border-color: var(--cc-accent) !important;

      .cc-product-card__glow {
        opacity: .9;
        transform: scale(1.25);
      }
    }
  }

  &--disabled {
    opacity: .75;
  }

  // A top accent line in the product's color.
  &::before {
    content: "";
    position: absolute;
    inset: 0 0 auto 0;
    height: 3px;
    background: var(--cc-accent-gradient);
  }

  :deep(.v-card__overlay) {
    display: none;
  }
}

.cc-product-card__glow {
  position: absolute;
  top: -60px;
  right: -60px;
  width: 180px;
  height: 180px;
  border-radius: 50%;
  background: var(--cc-accent-soft);
  filter: blur(30px);
  opacity: .6;
  pointer-events: none;
  transition: opacity .4s ease, transform .6s var(--cc-ease-out);
}

.cc-product-card__header {
  position: relative;
  display: flex;
  align-items: center;
  gap: 14px;
}

.cc-product-card__avatar {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  border-radius: 14px;
  background: var(--cc-accent-gradient);
  color: #fff;
  font-size: 1.3rem;
  font-weight: 800;
  box-shadow: 0 8px 20px -8px var(--cc-accent);
  animation: cc-pop var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: calc(var(--cc-delay, 0ms) + var(--cc-motion-step) * 1.5);
}

.cc-product-card__titles {
  flex: 1 1 auto;
  min-width: 0;
}

.cc-product-card__name {
  display: flex;
  align-items: center;
  font-size: 1.05rem;
  font-weight: 700;
  letter-spacing: -.01em;

  span {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}

.cc-product-card__endpoint {
  font-family: var(--cc-font-mono);
  font-size: .78rem;
  color: var(--cc-muted);
}

.cc-product-card__actions {
  display: flex;
  margin-right: -8px;
  opacity: .6;
  transition: opacity .2s ease;

  :deep(.v-btn) {
    margin: 0 !important;
  }
}

.cc-product-card:hover .cc-product-card__actions {
  opacity: 1;
}

.cc-product-card__description {
  position: relative;
  display: -webkit-box;
  min-height: 2.8em;
  margin: 0;
  overflow: hidden;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  font-size: .88rem;
  line-height: 1.4;
  color: var(--cc-muted);
}

.cc-product-card__problem {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 8px 12px;
  border-radius: var(--cc-radius-sm);
  background: rgba(var(--v-theme-error), .1);
  color: rgb(var(--v-theme-error));
  font-size: .82rem;
  font-weight: 550;
}

.cc-product-card__stats {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.cc-product-card__stat {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 10px;
  border-radius: 999px;
  background: rgba(var(--v-theme-on-surface), .05);
  font-size: .8rem;
  color: rgba(var(--v-theme-on-surface), .8);

  .v-icon {
    color: var(--cc-accent);
  }

  strong {
    font-weight: 700;
    color: rgb(var(--v-theme-on-surface));
  }
}

.cc-product-card__footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: auto;
  padding-top: 12px;
  border-top: 1px solid var(--cc-border);
}

.cc-product-card__admins {
  display: flex;
  padding-left: 6px;
}

.cc-product-card__admin {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  margin-left: -6px;
  border-radius: 50%;
  border: 2px solid rgb(var(--v-theme-surface));
  color: #fff;
  font-size: .7rem;
  font-weight: 750;
  transition: transform .25s var(--cc-ease-spring);

  &:hover {
    transform: translateY(-3px) scale(1.1);
    z-index: 20 !important;
  }

  &--more {
    background: rgba(var(--v-theme-on-surface), .12);
    color: rgb(var(--v-theme-on-surface));
  }
}

.cc-product-card__links {
  display: flex;
  gap: 2px;
}

.cc-product-card__link {
  color: var(--cc-muted);

  &:hover {
    color: var(--cc-accent);
  }
}
</style>
