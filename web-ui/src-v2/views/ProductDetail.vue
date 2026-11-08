<template>
  <v-container id="product" fluid>
    <router-view v-slot="{ Component, route }">
      <transition name="cc-page" mode="out-in">
        <component :is="Component" :key="pageKey(route)" />
      </transition>
    </router-view>
  </v-container>
</template>

<script setup>
// Web UI v2: replaces src/views/ProductDetail.vue (see vite.config.mjs).
// The pages of a product (runs, reports, a report, statistics) fade in
// like the top-level pages; the old page goes at once (see the cc-page
// transition in src-v2/styles/_animations.scss).

// The statistics tabs share one page: switching tabs is not a page change.
function pageKey(route) {
  return route.matched[1]?.path ?? route.path;
}
</script>
