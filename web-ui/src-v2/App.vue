<template>
  <v-app id="app">
    <route-progress />

    <TheHeader />

    <v-main class="d-flex flex-column cc-main">
      <router-view v-slot="{ Component }">
        <transition name="cc-page" mode="out-in">
          <keep-alive :include="keepAliveList">
            <component :is="Component" />
          </keep-alive>
        </transition>
      </router-view>
      <Notifications />
    </v-main>
  </v-app>
</template>

<script setup>
// Web UI v2: replaces src/App.vue (see vite.config.mjs). The layout is the
// original one; top-level pages fade in, a bar shows while a page loads,
// and the code of the main pages is loaded in the background.

import { computed, onMounted, ref, watch } from "vue";
import { useRouter } from "vue-router";
import { useStore } from "vuex";

import Notifications from "@/components/Notifications";
import { TheHeader } from "@/components/Layout";
import RouteProgress from "@v2/components/Layout/RouteProgress";

// The code of the main pages, loaded in the background (see below). The
// imports are the router's, so they share its files.
const PREFETCHED_PAGES = [
  () => import("@/views/RunList"),
  () => import("@/views/Reports"),
  () => import("@/views/ReportDetail"),
  () => import("@/views/Statistics"),
  () => import("@statistics/Overview/Overview")
];

const router = useRouter();
const keepAliveList = ref([]);
const store = useStore();
const isAuthenticated = computed(() => store.getters.isAuthenticated);

watch(isAuthenticated, newValue => {
  if (newValue) {
    keepAliveList.value.push("Products");
  } else {
    keepAliveList.value = [];
  }
});

// Once the first page is shown and the browser is idle, load the code of
// the main pages, so the first visit to each of them does not wait for its
// files. One file at a time: the server answers one request at a time, and
// the API calls of the current page must not queue behind them.
function whenIdle() {
  return new Promise(resolve => {
    if (window.requestIdleCallback) {
      window.requestIdleCallback(resolve, { timeout: 4000 });
    } else {
      setTimeout(resolve, 1500);
    }
  });
}

onMounted(async () => {
  await router.isReady();
  await new Promise(resolve => setTimeout(resolve, 1500));
  for (const load of PREFETCHED_PAGES) {
    await whenIdle();
    await load().catch(() => {});
  }
});
</script>

<style lang="scss">
html {
  overflow-y: auto;
}

html, body {
  height: 100%;
}

.cc-main {
  background: rgb(var(--v-theme-background));
}

.v-text-field.v-text-field--solo.small {
  & > .v-input__control {
    min-height: 30px;

    & > .v-input__slot {
      min-height: 30px;

      .v-select__selections {
        font-size: 0.75rem;
      }
    }
  }
}
</style>
