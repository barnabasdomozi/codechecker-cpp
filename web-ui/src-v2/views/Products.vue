<template>
  <v-container fluid class="cc-products">
    <div class="cc-products__toolbar">
      <v-text-field
        v-model="productNameSearch"
        class="cc-products__search"
        label="Search for products..."
        hide-details
        clearable
        density="compact"
        color="primary"
        variant="outlined"
        prepend-inner-icon="mdi-magnify"
      />

      <v-select
        v-model="sortKey"
        class="cc-products__sort"
        label="Sort by"
        :items="sortOptions"
        hide-details
        density="compact"
        variant="outlined"
      />

      <v-btn
        class="cc-products__square-btn"
        :icon="sortDesc ? 'mdi-sort-descending' : 'mdi-sort-ascending'"
        :title="sortDesc ? 'Descending' : 'Ascending'"
        :disabled="!sortKey"
        variant="text"
        @click="sortDesc = !sortDesc"
      />

      <v-btn-toggle
        v-model="viewMode"
        class="cc-products__view"
        mandatory
        aria-label="View"
      >
        <v-btn
          value="grid"
          prepend-icon="mdi-view-grid-outline"
          title="Show products as cards"
        >
          Cards
        </v-btn>
        <v-btn
          value="list"
          prepend-icon="mdi-view-list-outline"
          title="Show products as a table"
        >
          List
        </v-btn>
      </v-btn-toggle>

      <v-spacer />

      <div class="cc-products__actions">
        <edit-announcement-btn
          v-if="isSuperUser"
        />

        <edit-global-permission-btn
          v-if="isSuperUser"
        />

        <new-product-btn
          v-if="isSuperUser"
          :is-super-user="isSuperUser"
          @on-complete="refreshProducts"
        />

        <v-btn
          class="cc-products__square-btn"
          icon
          title="Reload products"
          color="primary"
          variant="flat"
          @click="refreshProducts"
        >
          <v-icon
            icon="mdi-refresh"
            :class="{ 'cc-spin': loading }"
          />
        </v-btn>
      </div>
    </div>

    <transition name="cc-page" mode="out-in">
      <div v-if="viewMode === 'grid'" key="grid">
        <div
          v-if="loading && !products.length"
          class="cc-products__grid"
        >
          <v-skeleton-loader
            v-for="n in 6"
            :key="n"
            class="cc-products__skeleton"
            type="list-item-avatar-two-line, paragraph, actions"
          />
        </div>

        <div
          v-else-if="!products.length"
          class="cc-products__empty"
        >
          <div class="cc-products__empty-icon">
            <v-icon icon="mdi-magnify-close" size="40" />
          </div>
          <h3>No products found</h3>
          <p class="cc-muted">
            <template v-if="productNameSearch">
              Nothing matches "{{ productNameSearch }}".
            </template>
            <template v-else>
              This server has no products yet.
            </template>
          </p>
        </div>

        <transition-group
          v-else
          name="cc-list"
          tag="div"
          class="cc-products__grid"
          :class="{ 'cc-products__grid--loading': loading }"
        >
          <product-card
            v-for="(product, i) in products"
            :key="product.endpoint"
            class="cc-rise"
            :style="{
              '--cc-delay': `calc(var(--cc-motion-step) * ${Math.min(i, 8)})`
            }"
            :product="product"
            :is-super-user="isSuperUser"
            @changed="refreshProducts"
          />
        </transition-group>

        <div
          v-if="itemsLength > 0"
          class="cc-products__pager"
        >
          <span class="cc-muted text-body-2">
            {{ firstShown }}–{{ lastShown }} of {{ itemsLength }}
          </span>
          <v-pagination
            v-if="pageCount > 1"
            v-model="page"
            :length="pageCount"
            :total-visible="7"
            density="comfortable"
            rounded="lg"
          />
          <v-select
            v-model="itemsPerPage"
            class="cc-products__per-page"
            :items="itemsPerPageOptions"
            label="Per page"
            hide-details
            density="compact"
            variant="outlined"
          />
        </div>
      </div>

      <v-card
        v-else
        key="list"
        class="cc-products__table-card"
      >
        <v-data-table-server
          v-model:page="page"
          v-model:items-per-page="itemsPerPage"
          v-model:sort-by="sortBy"
          class="products-table clickable-rows"
          :headers="headers"
          :items="products"
          :loading="loading"
          :items-per-page-options="itemsPerPageOptions"
          :items-length="itemsLength"
          loading-text="Loading products..."
          item-value="endpoint"
          @click:row="onRowClick"
        >
          <template #item.displayedName="{ item }">
            <product-name-column :product="item" />
          </template>

          <template #item.admins="{ item }">
            <span
              v-for="admin in item.admins"
              :key="admin"
              class="v-chip-max-width-wrapper"
            >
              <v-chip
                color="primary"
                class="mr-2 my-1"
                variant="tonal"
                size="small"
                prepend-icon="mdi-account-circle"
                :title="admin"
              >
                {{ admin }}
              </v-chip>
            </span>
          </template>

          <template #item.runCount="{ item }">
            <v-chip
              color="primary"
              size="small"
              variant="tonal"
            >
              {{ item.runCount }}
            </v-chip>
          </template>

          <template #item.latestStoreToProduct="{ item }">
            <span
              v-if="item.latestStoreToProduct"
              class="text-body-2"
            >
              {{ prettifyDate(item.latestStoreToProduct) }}
            </span>
          </template>

          <template v-slot:item.actions="{ item }">
            <div class="text-no-wrap" @click.stop>
              <edit-product-btn
                v-if="item.administrating"
                :product="item"
                :is-super-user="isSuperUser"
                @on-complete="refreshProducts"
              />
              <delete-product-btn
                v-if="isSuperUser"
                :product="item"
                @on-complete="refreshProducts"
              />
            </div>
          </template>
        </v-data-table-server>
      </v-card>
    </transition>
  </v-container>
</template>

<script setup>
import { computed, onMounted, ref, watch } from "vue";
import { useRoute, useRouter } from "vue-router";
import _ from "lodash";

import { authService, handleThriftError, prodService } from "@cc-api";
import { DBStatus, Permission } from "@cc/shared-types";
import { ProductSortMode } from "@cc/prod-types";

import { EditGlobalPermissionBtn } from "@/components/Product/Permission";
import {
  DeleteProductBtn,
  EditAnnouncementBtn,
  EditProductBtn,
  NewProductBtn,
  ProductNameColumn
} from "@/components/Product/";
import { useDateUtils } from "@/composables/useDateUtils";

import ProductCard from "@v2/components/Product/ProductCard";

// The same component name as the original page, so App.vue keeps it alive.
defineOptions({ name: "Products" });

const VIEW_STORAGE_KEY = "cc-web-ui-v2-products-view";

const route = useRoute();
const router = useRouter();
const { prettifyDate } = useDateUtils();

const itemsPerPageOptions = [
  { value: 12, title: "12" },
  { value: 24, title: "24" },
  { value: 48, title: "48" },
  { value: 96, title: "96" }
];

const sortOptions = [
  { value: null, title: "Accessible first" },
  { value: "displayedName", title: "Name" },
  { value: "runCount", title: "Number of runs" },
  { value: "latestStoreToProduct", title: "Latest store" }
];

const headers = ref([
  { title: "Name", key: "displayedName", sortable: true },
  { title: "Admins", key: "admins", sortable: false },
  {
    title: "Number of runs",
    key: "runCount",
    align: "center",
    sortable: true
  },
  {
    title: "Latest store to product",
    key: "latestStoreToProduct",
    align: "center",
    sortable: true
  },
  { title: "Actions", key: "actions", align: "center", sortable: false }
]);

const sortBy = ref([]);
const page = ref(parseInt(route.query["page"]) || 1);
const itemsPerPage = ref(
  parseInt(route.query["items-per-page"]) || itemsPerPageOptions[0].value
);
const productNameSearch = ref(null);
const loading = ref(false);
const products = ref([]);
const isSuperUser = ref(false);
const itemsLength = ref(0);
const viewMode = ref(readViewMode());

// The sort select and direction button edit the table's sortBy.
const sortKey = computed({
  get: () => sortBy.value?.[0]?.key ?? null,
  set: key => {
    sortBy.value = key
      ? [ { key, order: key === "displayedName" ? "asc" : "desc" } ]
      : [];
  }
});

const sortDesc = computed({
  get: () => sortBy.value?.[0]?.order === "desc",
  set: desc => {
    if (!sortKey.value) return;
    sortBy.value = [ { key: sortKey.value, order: desc ? "desc" : "asc" } ];
  }
});

const pageCount = computed(() =>
  Math.max(1, Math.ceil(itemsLength.value / itemsPerPage.value)));
const firstShown = computed(() =>
  itemsLength.value ? (page.value - 1) * itemsPerPage.value + 1 : 0);
const lastShown = computed(() =>
  Math.min(itemsLength.value, page.value * itemsPerPage.value));

watch(viewMode, mode => {
  try {
    localStorage.setItem(VIEW_STORAGE_KEY, mode);
  } catch {
    // The view is the default one again after a reload.
  }
});

watch([ page, itemsPerPage, sortBy ], () => {
  updateUrl();
  fetchProducts();
}, { deep: true });

const debouncedSearchHandler = _.debounce(() => {
  router.replace({
    query: {
      ...route.query,
      "name": productNameSearch.value || undefined
    }
  }).catch(() => {});

  // A new filter can shrink the result set below the current offset.
  page.value = 1;

  refreshProducts();
}, 500);

watch(productNameSearch, debouncedSearchHandler);

onMounted(() => {
  if (route.query["sort-by"]) {
    sortBy.value = [ {
      key: route.query["sort-by"],
      order: route.query["sort-desc"] === "true" ? "desc" : "asc"
    } ];
  }

  productNameSearch.value = route.query["name"] || null;

  checkPermissions();
  if (!productNameSearch.value) refreshProducts();
});

function readViewMode() {
  try {
    return localStorage.getItem(VIEW_STORAGE_KEY) === "list" ? "list" : "grid";
  } catch {
    return "grid";
  }
}

function updateUrl() {
  const defaultItemsPerPage = itemsPerPageOptions[0].value;
  const sort = sortBy.value?.[0];

  router.replace({
    query: {
      ...route.query,
      "page": page.value === 1 ? undefined : page.value,
      "items-per-page": itemsPerPage.value === defaultItemsPerPage
        ? undefined : itemsPerPage.value,
      "sort-by": sort?.key,
      "sort-desc": sort ? sort.order === "desc" : undefined,
    }
  }).catch(() => {});
}

function nameFilter() {
  return productNameSearch.value ? `*${productNameSearch.value}*` : null;
}

function getTotalProducts() {
  prodService.getClient().getProductCount(
    null,
    nameFilter(),
    handleThriftError(count => {
      itemsLength.value = count.toNumber();
    })
  );
}

function refreshProducts() {
  getTotalProducts();
  fetchProducts();
}

function fetchProducts() {
  loading.value = true;

  prodService.getClient().getProducts(
    null,
    nameFilter(),
    itemsPerPage.value,
    itemsPerPage.value * (page.value - 1),
    getSortingMode(),
    handleThriftError(_products => {
      products.value = _products.map(product => ({
        description: product.description_b64
          ? window.atob(product.description_b64) : null,
        displayedName: product.displayedName_b64
          ? window.atob(product.displayedName_b64) : null,
        ...product
      }));

      loading.value = false;
    }));
}

function getSortingMode() {
  const sort = sortBy.value?.[0];
  const desc = sort?.order === "desc";

  switch (sort?.key) {
  case "displayedName":
    return desc ? ProductSortMode.NAMES_DESC : ProductSortMode.NAMES_ASC;
  case "runCount":
    return desc ? ProductSortMode.RUNS_DESC : ProductSortMode.RUNS_ASC;
  case "latestStoreToProduct":
    return desc
      ? ProductSortMode.LATEST_STORE_DESC
      : ProductSortMode.LATEST_STORE_ASC;
  default:
    // No column selected -> accessible-to-current-user products first.
    return ProductSortMode.ACCESSRIGHT;
  }
}

function onRowClick(event, { item }) {
  if (!item.accessible || item.databaseStatus !== DBStatus.OK) return;

  router.push({ name: "runs", params: { endpoint: item.endpoint } });
}

function checkPermissions() {
  authService.getClient().hasPermission(Permission.SUPERUSER, "",
    handleThriftError(superUser => {
      isSuperUser.value = superUser;

      if (superUser) return;

      prodService.getClient().isAdministratorOfAnyProduct(
        handleThriftError(isAdmin => {
          if (!isAdmin) {
            headers.value = headers.value.filter(h => h.key !== "actions");
          }
        }));
    }));
}
</script>

<style lang="scss" scoped>
.cc-products__toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 10px;
  margin-bottom: 20px;
  animation: cc-rise var(--cc-motion-enter) var(--cc-ease-enter) both;
}

.cc-products__search {
  flex: 1 1 220px;
  max-width: 380px;
  min-width: 180px;
}

.cc-products__sort {
  flex: 0 0 190px;
}

.cc-products__view {
  height: 40px !important;

  :deep(.v-btn) {
    height: 100% !important;
    padding: 0 14px;
  }
}

// Icon buttons as tall as the fields and buttons next to them.
.cc-products__square-btn {
  width: 40px !important;
  height: 40px !important;
  border-radius: var(--cc-radius-sm) !important;
}

.cc-products__actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;

  :deep(.v-btn) {
    margin: 0 !important;
  }
}

.cc-products__grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 20px;
  transition: opacity .3s ease;

  &--loading {
    opacity: .55;
  }
}

.cc-products__skeleton {
  border-radius: var(--cc-radius-lg);
  border: 1px solid var(--cc-border);
  background: rgb(var(--v-theme-surface));
}

.cc-products__empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 64px 16px;
  text-align: center;
  animation: cc-pop var(--cc-motion-enter) var(--cc-ease-enter) both;

  h3 {
    margin: 18px 0 4px;
    font-size: 1.2rem;
    font-weight: 700;
  }
}

.cc-products__empty-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 84px;
  height: 84px;
  border-radius: 50%;
  background: var(--cc-gradient-soft);
  color: rgb(var(--v-theme-primary));
}

.cc-products__pager {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-top: 28px;
}

.cc-products__per-page {
  flex: 0 0 120px;
}

.cc-products__table-card {
  padding: 8px;
}

.cc-spin {
  animation: cc-spin .8s linear infinite;
}

.v-chip-max-width-wrapper {
  display: inline-block;
  max-width: 150px;

  :deep(.v-chip__content) {
    display: inline-block !important;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
}

.clickable-rows :deep(tbody tr) {
  cursor: pointer;
}

@media (max-width: 700px) {
  .cc-products__sort {
    flex: 1 1 160px;
  }
}
</style>
