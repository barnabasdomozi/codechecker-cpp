// Paging of the long statistics tables of web UI v2. BaseStatisticsTable of
// src/ shows every row, which takes seconds for thousands of rows. Its
// attributes reach Vuetify's data table and win over its own settings, so
// a table turns paging on by binding these. Sorting and the total row
// still cover all rows.

export const PAGE_SIZE = 50;

export function paginationAttrs(itemCount) {
  return {
    "items-per-page": PAGE_SIZE,
    "items-per-page-options": [
      { value: 25, title: "25" },
      { value: 50, title: "50" },
      { value: 100, title: "100" },
      { value: -1, title: "All" }
    ],
    // Short tables need no footer.
    "hide-default-footer": itemCount <= PAGE_SIZE
  };
}
