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

    <nav
      v-if="crumbs.length"
      class="cc-crumbs"
      aria-label="Breadcrumb"
    >
      <transition-group name="cc-crumb">
        <span
          v-for="(crumb, i) in crumbs"
          :key="crumb.key"
          class="cc-crumbs__crumb"
        >
          <v-icon
            class="cc-crumbs__sep"
            icon="mdi-chevron-right"
            size="18"
          />
          <router-link
            v-if="i < crumbs.length - 1"
            :to="crumb.to"
            class="cc-crumbs__item cc-crumbs__item--link"
            :title="crumb.title"
            :aria-label="crumb.title"
          >
            <v-icon
              v-if="crumb.icon"
              :icon="crumb.icon"
              size="16"
            />
            <span
              v-if="!crumb.iconOnly"
              class="cc-crumbs__text"
            >
              {{ crumb.title }}
            </span>
          </router-link>
          <span
            v-else
            class="cc-crumbs__item cc-crumbs__item--current"
            :title="crumb.title"
            aria-current="page"
          >
            <v-icon
              v-if="crumb.icon"
              :icon="crumb.icon"
              size="16"
            />
            <span class="cc-crumbs__text">{{ crumb.title }}</span>
          </span>
        </span>
      </transition-group>
    </nav>

    <v-spacer />

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
// vite.config.mjs). The items and their behavior are the original ones.

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

const menuButtons = ref([
  {
    name: "Products",
    query_namespace: "products",
    icon: "mdi-briefcase-outline",
    route: "products",
    active: [ "products" ],
    exact: true,
    hide: [ "products", "login", "404" ]
  },
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
    active: [ "reports" ],
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

const currentProductDisplayName = computed(() => {
  return currentProduct.value
    ? window.atob(currentProduct.value.displayedName_b64)
    : null;
});

// The title of each page in the breadcrumbs, by route name.
const PAGE_TITLES = {
  "runs": "Runs",
  "main_runs": "Runs",
  "reports": "Reports",
  "report-detail": "Report",
  // Statistics tabs, after a "Statistics" crumb.
  "product-overview": "Overview",
  "checker-statistics": "Checkers",
  "severity-statistics": "Severity",
  "component-statistics": "Components",
  "checker-coverage-statistics": "Checker Coverage",
  "guideline-statistics": "Guidelines",
  "cleanup-plan": "Cleanup Plan",
  "review-status-rules": "Review Status Rules",
  "source-component": "Source Component",
  "userguide": "User guide",
  "new-features": "New features",
  "404": "Not found"
};

// The tabs of the statistics page.
const STATISTICS_PAGES = [
  "product-overview",
  "checker-statistics",
  "severity-statistics",
  "component-statistics",
  "checker-coverage-statistics",
  "guideline-statistics"
];

// A product page with the query it had last, as the menu buttons use.
function savedLocation(name, endpoint) {
  const item = menuButtons.value.find(i => i.route === name);
  const query = queries.value[item.query_namespace];
  return {
    name,
    params: { endpoint },
    query: query === undefined ? item.query || {} : query
  };
}

// Products > product > (Statistics or Reports) > page. Each crumb but the
// last one links to its page.
const crumbs = computed(() => {
  const name = route.name;
  if (!name || name === "login" || name === "oauthlogin") return [];

  // The first crumb is only an icon, unless the products page is shown.
  const result = [ {
    key: "products",
    title: "Products",
    icon: "mdi-briefcase-outline",
    iconOnly: name !== "products",
    to: { name: "products" }
  } ];

  const endpoint = route.params.endpoint;
  if (endpoint && PAGE_TITLES[name]) {
    result.push({
      key: `product-${endpoint}`,
      title: currentProductDisplayName.value || endpoint,
      to: { name: "runs", params: { endpoint } }
    });

    if (STATISTICS_PAGES.includes(name)) {
      result.push({
        key: "statistics",
        title: "Statistics",
        to: savedLocation("statistics", endpoint)
      });
    } else if (name === "report-detail") {
      result.push({
        key: "reports",
        title: "Reports",
        to: savedLocation("reports", endpoint)
      });
    }
  }

  // The product's own page is its run list: no second crumb for it.
  if (PAGE_TITLES[name] && !(endpoint && name === "main_runs")) {
    result.push({ key: `page-${name}`, title: PAGE_TITLES[name] });
  }

  return result;
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

const showConfigItems = computed(() => {
  return ![ "products", "login", "404" ].includes(route.name) &&
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
  margin-left: 2px;
  font-weight: 500;
  opacity: .75;
}

// The breadcrumbs after the title.
.cc-crumbs {
  display: flex;
  align-items: center;
  gap: 2px;
  min-width: 0;
  margin-left: 10px;
  overflow: hidden;
  white-space: nowrap;
}

.cc-crumbs__crumb {
  display: inline-flex;
  align-items: center;
  min-width: 0;

  // The current page is never shortened; earlier crumbs give way first.
  &:last-child {
    flex: 0 0 auto;
  }
}

.cc-crumbs__sep {
  flex: 0 0 auto;
  color: rgba(255, 255, 255, .55);
}

.cc-crumbs__item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  padding: 5px 10px;
  border-radius: var(--cc-radius-sm);
  font-size: .9rem;
  font-weight: 550;
  text-decoration: none !important;
  transition: background-color .2s ease, color .2s ease;

  &--link {
    flex: 0 1 auto;
    color: rgba(255, 255, 255, .75) !important;

    &:hover {
      background: rgba(255, 255, 255, .12);
      color: #fff !important;
    }
  }

  &--current {
    flex: 0 0 auto;
    color: #fff;
    font-weight: 700;
  }
}

.cc-crumbs__text {
  max-width: 220px;
  overflow: hidden;
  text-overflow: ellipsis;
}

// A new crumb slides in; leaving ones vanish at once, so they never push
// the new ones aside.
.cc-crumb-enter-active {
  transition: opacity .3s ease, transform .35s var(--cc-ease-out);
}

.cc-crumb-enter-from {
  opacity: 0;
  transform: translateX(-8px);
}

.cc-crumb-leave-active {
  display: none;
}

.cc-header__nav {
  display: flex;
  align-items: center;
  gap: 4px;
  margin-right: 4px;
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
