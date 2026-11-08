<template>
  <v-container
    class="text-center cc-not-found"
    fill-height
  >
    <v-row align="center">
      <v-col>
        <h1 class="cc-not-found__title">
          Whoops, <span class="cc-gradient-text">404</span>
        </h1>

        <p class="cc-not-found__text cc-muted">
          The page you were looking for does not exist!
        </p>

        <v-btn
          :to="{ name: 'products' }"
          class="cc-not-found__btn"
          color="primary"
          variant="outlined"
          prepend-icon="mdi-briefcase-outline"
        >
          Back to products
        </v-btn>
      </v-col>
    </v-row>
  </v-container>
</template>

<style lang="scss" scoped>
// The layout of the original page; its parts rise in one after the other.
.cc-not-found__title {
  margin-top: 64px;
  font-size: clamp(2.5rem, 6vw, 3.75rem);
  font-weight: 800;
  letter-spacing: -.04em;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
}

.cc-not-found__text {
  margin: 8px 0 24px;
  font-size: 1.05rem;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-motion-step);
}

.cc-not-found__btn {
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: calc(var(--cc-motion-step) * 2);
}
</style>
