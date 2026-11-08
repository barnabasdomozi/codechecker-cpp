<template>
  <v-container
    class="fill-height"
    fluid
  >
    <v-row
      align="center"
      justify="center"
    >
      <v-col
        cols="12"
        sm="8"
        md="4"
        lg="3"
      >
        <v-card
          class="cc-login pa-8"
          variant="flat"
        >
          <v-card-title>
            <div class="text-center mt-6 mb-4">
              <div id="avatar" class="cc-login__avatar">
                <v-icon :size="64">
                  mdi-account
                </v-icon>
              </div>
              <div class="cc-login__title">
                Login
              </div>
            </div>
          </v-card-title>

          <v-card-actions
            v-if="providers.length !== 0"
            id="btn-container"
            class="d-flex justify-center flex-column cc-login__step"
            style="--cc-delay: calc(var(--cc-motion-step) * 2)"
          >
            <v-btn
              block
              size="x-large"
              color="primary"
              variant="flat"
              @click="ssoButtonHandleClickEvent"
            >
              {{ ssoButtonText }}
            </v-btn>
            <a
              href="#"
              class="text-button text-no-wrap cc-login__toggle"
              @click.prevent="toggleOtherLoginOptions"
            >
              {{ optionsShow ? 'Hide other options' : 'Show other options' }}
            </a>
          </v-card-actions>

          <v-expand-transition>
            <div v-if="optionsShow">
              <v-card-text class="px-0 pb-0">
                <v-form v-model="isDataValid">
                  <v-text-field
                    v-model="username"
                    class="cc-login__step"
                    style="--cc-delay: calc(var(--cc-motion-step) * 2)"
                    autocomplete="username"
                    label="Username"
                    name="username"
                    prepend-inner-icon="mdi-account-outline"
                    type="text"
                    required
                    variant="outlined"
                    :rules="[() => !!username || 'This field is required']"
                    :placeholder="placeholder"
                    @keyup.enter="login"
                  />

                  <v-text-field
                    id="password"
                    v-model="password"
                    class="cc-login__step"
                    style="--cc-delay: calc(var(--cc-motion-step) * 3)"
                    autocomplete="current-password"
                    label="Password"
                    name="password"
                    prepend-inner-icon="mdi-lock-outline"
                    :append-inner-icon="passwordIcon"
                    :type="showPassword ? 'text' : 'password'"
                    required
                    variant="outlined"
                    :rules="[() => !!password || 'This field is required']"
                    :placeholder="placeholder"
                    @click:append-inner="showPassword = !showPassword"
                    @keyup.enter="login"
                  />
                </v-form>
              </v-card-text>
              <v-card-actions
                id="btn-container"
                class="d-flex justify-center flex-column px-0 cc-login__step"
                style="--cc-delay: calc(var(--cc-motion-step) * 4)"
              >
                <v-btn
                  id="login-btn"
                  block
                  size="x-large"
                  color="primary"
                  variant="flat"
                  :loading="loggingIn"
                  @click="login()"
                >
                  Login
                </v-btn>
              </v-card-actions>
            </div>
          </v-expand-transition>
        </v-card>
      </v-col>
    </v-row>

    <v-dialog
      v-model="dialog"
      width="500"
      :scrollable="true"
    >
      <v-card>
        <v-card-title class="d-flex align-center bg-primary">
          Login Methods
          <v-spacer />
          <v-btn
            variant="text"
            icon="mdi-close"
            @click="dialog = false"
          />
        </v-card-title>
        <v-card-text>
          <v-btn
            v-for="provider in providers"
            :id="`login-btn-${provider}`"
            :key="provider"
            block
            size="x-large"
            color="primary"
            variant="tonal"
            class="mt-3"
            @click="oauth(provider)"
          >
            Login with {{ provider }}
          </v-btn>
        </v-card-text>
      </v-card>
    </v-dialog>
  </v-container>
</template>

<script setup>
import { computed, onMounted, ref, watch } from "vue";
import { useStore } from "vuex";
import { useRoute, useRouter } from "vue-router";
import { LOGIN } from "@/store/actions.type";
import { ADD_ERROR, ADD_NOTIFICATION } from "@/store/mutations.type";
import { authService, handleThriftError } from "@cc-api";

const store = useStore();
const route = useRoute();
const router = useRouter();

const placeholder = ref("");
const username = ref("");
const password = ref("");
const showPassword = ref(false);
const isDataValid = ref(false);
const providers = ref([]);
const dialog = ref(false);
const optionsShow = ref(false);
const loggingIn = ref(false);

const authParams = computed(() => store.getters.authParams);
const isAuthenticated = computed(() => store.getters.isAuthenticated);

const passwordIcon = computed(() =>
  showPassword.value ? "mdi-eye-off" : "mdi-eye");

const ssoButtonText = computed(() => {
  return providers.value.length === 1 ?
    `Login with ${providers.value[0]}` : "SSO Login";
});

watch(username, () => {
  resetPlaceholder();
});

watch(password, () => {
  resetPlaceholder();
});

onMounted(() => {
  fixAutocomplete();
  getProviders();
  if (isAuthenticated.value && authParams.value?.sessionStillActive) {
    const returnTo = route.query["return_to"];
    router.replace(returnTo || { name: "products" });
  }
});

function toggleOtherLoginOptions() {
  optionsShow.value = !optionsShow.value;
}

function ssoButtonHandleClickEvent() {
  if (providers.value.length === 1) {
    oauth(providers.value[0]);
  } else {
    dialog.value = true;
  }
}

function login() {
  if (!username.value || !password.value) {
    return;
  }

  loggingIn.value = true;
  store.dispatch(LOGIN,
    { username: username.value, password: password.value, type: "password" }
  ).then(() => {
    store.commit(ADD_NOTIFICATION, {
      type: "success",
      message: "Successfully logged in!"
    });

    const returnTo = route.query["return_to"];
    router.replace(returnTo || { name: "products" });
  }).catch(err => {
    store.commit(ADD_ERROR, {
      title: "Login failed",
      message: err.message
    });
  }).finally(() => {
    loggingIn.value = false;
  });
}

function getProviders() {
  new Promise(
    resolve => {
      authService.getClient().getOauthProviders(
        handleThriftError(providers => {
          resolve(providers);
        }));
    }
  ).then(_providers => {
    if (_providers) {
      providers.value = _providers;
    }
    optionsShow.value = providers.value.length === 0;
  }).catch(err => {
    store.commit(ADD_ERROR, {
      title: "Could not load login providers",
      message: `Providers list was passed incorrectly. ${err.message}`
    });
  });
}

function oauth(provider) {
  const returnTo = route.query["return_to"];
  if (returnTo) {
    localStorage.setItem("return_to", returnTo);
  }
  new Promise(resolve => {
    localStorage.setItem("oauth_provider", provider);
    authService.getClient().createLink(provider,
      handleThriftError(url => {
        resolve(url);
      }));
  }).then(url => {
    if (url) {
      window.location.href = url;
    } else {
      store.commit(ADD_ERROR, {
        title: "Login failed",
        message: `Server returned an invalid URL: ${url}`
      });
    }
  }).catch(err => {
    store.commit(ADD_ERROR, {
      title: "Login failed",
      message: `Failed to access link. ${err.message}`
    });
  });
}

/**
 * Set placeholder when browser autocompletes username or password to raise
 * v-text-field labels.
 * See: https://github.com/vuetifyjs/vuetify/issues/3679
 */
function fixAutocomplete() {
  let times = 0;
  const interval = setInterval(() => {
    times += 1;
    if (placeholder.value || times === 20) {
      clearInterval(interval);
    }

    const username_query =
        document.querySelector("input[name=\"username\"]:-webkit-autofill");
    const password_query =
        document.querySelector("input[name=\"password\"]:-webkit-autofill");

    if (username_query || password_query) {
      placeholder.value = "` `";
    }
  }, 100);
}

/**
 * We set the placeholder back to null if the user edited this field.
 * This way the label of the text field will be moved properly.
 * See the fixAutocomplete function for more information.
 */
function resetPlaceholder() {
  placeholder.value = null;
}
</script>

<style lang="scss" scoped>
// The layout of the original login page: a card in the middle with the
// avatar over its top edge.
.cc-login {
  overflow: visible;
  border-radius: 24px !important;
  box-shadow: var(--cc-shadow-lg) !important;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
}

.cc-login__avatar {
  position: absolute;
  top: -56px;
  left: 0;
  right: 0;
  z-index: 9;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 104px;
  height: 104px;
  margin: 0 auto;
  border-radius: 50%;
  border: 6px solid rgb(var(--v-theme-background));
  background: var(--cc-gradient);
  color: #fff;
  box-shadow: var(--cc-shadow-brand);
  animation: cc-pop var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-motion-step);
}

.cc-login__title {
  margin-top: 24px;
  font-size: 1.8rem;
  font-weight: 750;
  letter-spacing: -.02em;
  color: var(--cc-muted);
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: calc(var(--cc-motion-step) * 2);
}

// Parts of the form appear one after the other.
.cc-login__step {
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
  animation-delay: var(--cc-delay, 0ms);
}

.cc-login__toggle {
  margin-top: 8px;
  color: rgb(var(--v-theme-primary));
  text-decoration: none;
}

#btn-container > button {
  margin: 0 !important;
  margin-top: 10px !important;
}

#login-btn {
  font-size: 1.1em;
}
</style>
