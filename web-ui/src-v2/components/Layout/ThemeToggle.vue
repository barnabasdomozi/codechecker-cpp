<template>
  <v-btn
    icon
    variant="text"
    class="cc-theme-toggle"
    :title="isDark ? 'Switch to light mode' : 'Switch to dark mode'"
    @click="toggleTheme"
  >
    <transition name="cc-theme-icon" mode="out-in">
      <v-icon
        v-if="isDark"
        key="dark"
        icon="mdi-weather-night"
      />
      <v-icon
        v-else
        key="light"
        icon="mdi-white-balance-sunny"
      />
    </transition>
  </v-btn>
</template>

<script setup>
import { useThemeMode } from "@v2/composables/useThemeMode";

const { isDark, toggleTheme } = useThemeMode();
</script>

<style lang="scss" scoped>
.cc-theme-icon-enter-active,
.cc-theme-icon-leave-active {
  transition: transform .35s var(--cc-ease-spring), opacity .2s ease;
}

.cc-theme-icon-enter-from {
  opacity: 0;
  transform: rotate(-120deg) scale(.4);
}

.cc-theme-icon-leave-to {
  opacity: 0;
  transform: rotate(120deg) scale(.4);
}
</style>
