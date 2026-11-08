<template>
  <div
    class="cc-route-progress"
    :class="{ 'cc-route-progress--active': active }"
    :style="{ transform: `scaleX(${progress})` }"
    aria-hidden="true"
  />
</template>

<script setup>
import { onBeforeUnmount, ref } from "vue";
import { useRouter } from "vue-router";

// A thin bar at the top of the window while the router loads a page. It
// creeps towards the end and completes when the navigation is done.

const router = useRouter();

const active = ref(false);
const progress = ref(0);

let timer = null;
let hideTimer = null;

function start() {
  clearTimeout(hideTimer);
  clearInterval(timer);
  active.value = true;
  progress.value = 0.08;
  timer = setInterval(() => {
    progress.value += (0.9 - progress.value) * 0.12;
  }, 120);
}

function done() {
  if (!active.value) return;
  clearInterval(timer);
  progress.value = 1;
  hideTimer = setTimeout(() => {
    active.value = false;
    progress.value = 0;
  }, 350);
}

// Filters change the query all the time: only a new page shows the bar.
const removeBefore = router.beforeEach((to, from) => {
  if (to.path !== from.path) start();
});
const removeAfter = router.afterEach(() => done());
const removeError = router.onError(() => done());

onBeforeUnmount(() => {
  removeBefore();
  removeAfter();
  removeError();
  clearInterval(timer);
  clearTimeout(hideTimer);
});
</script>

<style lang="scss" scoped>
.cc-route-progress {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  z-index: 3000;
  height: 3px;
  transform-origin: left center;
  background: var(--cc-gradient);
  background-size: 200% 100%;
  box-shadow: 0 0 12px rgba(59, 130, 246, .7);
  opacity: 0;
  pointer-events: none;
  transition: transform .3s var(--cc-ease-out), opacity .3s ease;

  &--active {
    opacity: 1;
    animation: cc-gradient-pan 1.5s linear infinite;
  }
}
</style>
