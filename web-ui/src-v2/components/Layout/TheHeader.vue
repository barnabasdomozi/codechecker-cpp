<template>
  <v-app-bar
    class="cc-header"
    extension-height="24"
    height="64"
    flat
  >
    <template
      v-if="announcement && announcement.length"
      v-slot:extension
    >
      <div class="cc-announcement">
        <v-icon size="x-small">
          mdi-bullhorn-outline
        </v-icon>
        <span class="cc-announcement__text">
          {{ announcement }}
        </span>
      </div>
    </template>

    <router-link
      :to="{ name: 'products' }"
      class="cc-header__brand"
    >
      <v-avatar
        class="cc-header__logo"
        size="36px"
        :image="logo"
        color="transparent"
      />
      <span class="cc-header__title">
        CodeChecker
        <span class="cc-header__version">{{ packageVersion }}</span>
      </span>
    </router-link>

    <router-link
      v-if="endpoint"
      :to="{ name: 'products', query: queries.products ?? {} }"
      class="cc-header__product"
      title="Show all products"
    >
      <v-icon icon="mdi-briefcase-outline" size="15" />
      <span class="cc-header__product-name">
        <span class="cc-header__product-label">Product:</span>
        <template v-if="currentProductDisplayName">
          {{ currentProductDisplayName }}
        </template>
        <span
          v-else
          class="cc-header__product-skeleton"
          aria-label="Loading"
        />
      </span>
      <v-icon
        class="cc-header__product-switch"
        icon="mdi-swap-horizontal"
        size="15"
      />
    </router-link>


    <nav
      v-if="showMenuItems"
      class="cc-header__nav"
    >
      <v-btn
        v-for="(item, i) in menuItems"
        :key="item.name"
        :to="{
          name: item.route,
          query: queries[item.query_namespace] === undefined
            ? item.query || {}
            : queries[item.query_namespace]
        }"
        :class="{
          'cc-header__link--active': item.active.includes($route.name)
        }"
        :style="{ '--cc-delay': `calc(var(--cc-motion-step) * ${i})` }"
        class="cc-header__link"
        :exact="item.exact"
        variant="text"
        :prepend-icon="item.icon"
      >
        {{ item.name }}
      </v-btn>
    </nav>

    <v-menu
      v-if="showConfigItems"
    >
      <template v-slot:activator="{ props }">
        <v-btn
          v-bind="props"
          variant="text"
          class="cc-header__link"
          :class="{
            'cc-header__link--active':
              configureMenuItems.map(c => c.route).includes($route.name)
          }"
          prepend-icon="mdi-cog-outline"
          append-icon="mdi-menu-down"
        >
          Configuration
        </v-btn>
      </template>

      <v-list>
        <v-list-item
          v-for="item in configureMenuItems"
          :key="item.title"
          :to="{ name: item.route }"
          exact
          :prepend-icon="item.icon"
          prepend-gap="8"
        >
          <v-list-item-title>{{ item.title }}</v-list-item-title>
        </v-list-item>
      </v-list>
    </v-menu>

    <v-spacer />

    <v-divider
      v-if="showUserInfo && menuItems.length"
      class="cc-header__divider mx-2"
      vertical
      inset
    />

    <user-info-menu
      v-if="showUserInfo"
    />

    <theme-toggle />

    <v-menu>
      <template v-slot:activator="{ props }">
        <v-btn
          v-bind="props"
          class="mr-2"
          icon
        >
          <v-icon>mdi-dots-vertical</v-icon>
        </v-btn>
      </template>

      <header-menu-items />
    </v-menu>
  </v-app-bar>
</template>

<script setup>
// Web UI v2: replaces src/components/Layout/TheHeader.vue (see
// vite.config.mjs). The title, the product (a link to the products page)
// and the product's pages on the left; the user, theme and menu buttons on
// the right. The items and their
// behavior are the original ones.

import { computed, onMounted, ref } from "vue";
import { useRoute } from "vue-router";
import { useStore } from "vuex";

import logo from "@/assets/logo.png";
import { GET_ANNOUNCEMENT, GET_PACKAGE_VERSION } from "@/store/actions.type";

import { defaultReportFilterValues } from "@/components/Report/ReportFilter";
import HeaderMenuItems from "@/components/Layout/HeaderMenuItems";
import UserInfoMenu from "@/components/Layout/UserInfoMenu";
import ThemeToggle from "./ThemeToggle";

const store = useStore();
const route = useRoute();

// The pages of the product. The products page is reached from the
// product pill (and the title).
const menuButtons = ref([
  {
    name: "Runs",
    query_namespace: "runs",
    icon: "mdi-run-fast",
    route: "runs",
    active: [ "runs", "main_runs" ],
    exact: true,
    hide: [ "products", "login", "404" ]
  },
  {
    name: "Reports",
    query_namespace: "report_filter",
    icon: "mdi-bug",
    route: "reports",
    // The report page belongs to Reports.
    active: [ "reports", "report-detail" ],
    exact: true,
    query: defaultReportFilterValues,
    hide: [ "products", "login", "404" ]
  },
  {
    name: "Statistics",
    query_namespace: "report_filter",
    icon: "mdi-chart-line",
    route: "statistics",
    active: [
      "statistics",
      "product-overview",
      "checker-statistics",
      "severity-statistics",
      "component-statistics",
      "checker-coverage-statistics",
      "guideline-statistics"
    ],
    exact: false,
    query: defaultReportFilterValues,
    hide: [ "products", "login", "404" ]
  }
]);

const configureMenuItems = ref([
  {
    title: "Cleanup Plan",
    icon: "mdi-sign-direction",
    route: "cleanup-plan"
  },
  {
    title: "Review Status Rules",
    icon: "mdi-format-list-checkbox",
    route: "review-status-rules"
  },
  {
    title: "Source Component",
    icon: "mdi-puzzle-outline",
    route: "source-component"
  }
]);

const queries = computed(() => store.getters.queries);
const authParams = computed(() => store.getters.authParams);
const isAuthenticated = computed(() => store.getters.isAuthenticated);
const announcement = computed(() => store.getters.announcement);
const packageVersion = computed(() => store.getters.packageVersion);
const currentProduct = computed(() => store.getters.currentProduct);

// The product of the page, if it belongs to one.
const endpoint = computed(() => route.params.endpoint);

// The name of the page's product. Until the product of this endpoint is
// loaded, the pill shows a placeholder (never the previous product).
const currentProductDisplayName = computed(() => {
  const product = currentProduct.value;
  return product && product.endpoint === endpoint.value
    ? window.atob(product.displayedName_b64)
    : null;
});

const menuItems = computed(() => {
  if (!route.name) return [];

  return menuButtons.value.filter(item => {
    return !item.hide || !item.hide.includes(route.name);
  });
});

const showMenuItems = computed(() => {
  return !authParams.value?.requiresAuthentication || isAuthenticated.value;
});

const showUserInfo = computed(() => {
  return authParams.value?.requiresAuthentication && isAuthenticated.value;
});

// Shown with the page links, once the page is known.
const showConfigItems = computed(() => {
  return !!route.name &&
    ![ "products", "login", "404" ].includes(route.name) &&
    showMenuItems.value;
});

onMounted(() => {
  store.dispatch(GET_ANNOUNCEMENT);
  store.dispatch(GET_PACKAGE_VERSION);
});
</script>

<style lang="scss" scoped>
.cc-header {
  background: linear-gradient(
    100deg, #1e40af 0%, #1d4ed8 55%, #2563eb 100%
  ) !important;
  color: #fff !important;
  box-shadow: 0 6px 24px -10px rgba(30, 64, 175, .45) !important;

  :deep(.v-toolbar__content) {
    padding-left: 12px;
  }
}

.cc-header__brand {
  display: flex;
  align-items: center;
  gap: 12px;
  color: inherit !important;
  text-decoration: none !important;
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;
}

.cc-header__title {
  font-size: 1.15rem;
  font-weight: 750;
  letter-spacing: -.02em;
  white-space: nowrap;
}

.cc-header__version {
  display: inline-block;
  // The room of the version while it loads, so nothing moves when it comes.
  min-width: 2.2em;
  margin-left: 2px;
  font-weight: 500;
  opacity: .75;

  // Room for the page links on narrow screens.
  @media (max-width: 1100px) {
    display: none;
  }
}

// The product of the page, after the title, as the original header's chip.
// It links to the products page, where another product can be chosen.
.cc-header__product {
  display: inline-flex;
  flex: 0 0 auto;
  align-items: center;
  gap: 6px;
  min-width: 0;
  height: 28px;
  margin-left: 14px;
  padding: 0 10px;
  border: 1px solid rgba(255, 255, 255, .28);
  border-radius: 999px;
  background: rgba(255, 255, 255, .1);
  color: #fff !important;
  font-size: .82rem;
  font-weight: 600;
  white-space: nowrap;
  text-decoration: none !important;
  animation: cc-fade var(--cc-motion-enter) var(--cc-ease-enter) both;
  transition: background-color .2s ease, border-color .2s ease;

  &:hover {
    border-color: rgba(255, 255, 255, .5);
    background: rgba(255, 255, 255, .2);

    .cc-header__product-switch {
      opacity: 1;
    }
  }

  &:focus-visible {
    outline: none;
    box-shadow: 0 0 0 3px rgba(255, 255, 255, .45);
  }
}

.cc-header__product-skeleton {
  display: inline-block;
  width: 56px;
  height: 10px;
  border-radius: 5px;
  background: rgba(255, 255, 255, .25);
  vertical-align: -1px;
  animation: cc-skeleton-pulse 1.2s ease-in-out infinite;
}

@keyframes cc-skeleton-pulse {
  0%, 100% { opacity: .5; }
  50% { opacity: 1; }
}

// A margin, not a space: the template drops the space before the name.
.cc-header__product-label {
  margin-right: .3em;
  font-weight: 500;
  opacity: .75;
}

.cc-header__product-switch {
  opacity: .7;
  transition: opacity .2s ease;
}

.cc-header__product-name {
  max-width: 220px;
  overflow: hidden;
  text-overflow: ellipsis;
}

// The page links, on the left after the title and the product.
.cc-header__nav {
  display: flex;
  align-items: center;
  gap: 4px;
  margin: 0 4px 0 16px;
}

// The page links: white text, a glass pill on hover, a solid one with a
// bottom line for the current page.
.cc-header__link {
  position: relative;
  color: rgba(255, 255, 255, .85) !important;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay, 0ms);

  :deep(.v-btn__overlay) {
    display: none;
  }

  &::after {
    content: "";
    position: absolute;
    left: 14px;
    right: 14px;
    bottom: 3px;
    height: 2px;
    border-radius: 2px;
    background: #fff;
    transform: scaleX(0);
    transition: transform .3s var(--cc-ease-out);
  }

  &:hover {
    background: rgba(255, 255, 255, .1);
    color: #fff !important;
  }

  &--active {
    background: rgba(255, 255, 255, .18);
    color: #fff !important;

    &::after {
      transform: scaleX(1);
    }
  }
}

// Narrow screens: tighter page links.
@media (max-width: 1100px) {
  .cc-header__nav {
    gap: 0;
    margin-left: 10px;
  }

  .cc-header__link {
    padding-inline: 10px;
  }
}

.cc-header__divider {
  border-color: rgba(255, 255, 255, .35) !important;
  opacity: 1;
}

.cc-announcement {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  width: 100%;
  height: 24px;
  background: linear-gradient(90deg, #f59e0b, #f97316);
  color: #3b1d00;
  font-size: .78rem;
  font-weight: 600;
  animation: cc-slide-down var(--cc-motion-enter) var(--cc-ease-enter) both;
}

.cc-announcement__text {
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
</style>
