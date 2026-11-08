// Totals of statistics rows that have review status counts (checker,
// severity and component statistics), for the summary tiles.
export function reviewStatusTotals(items) {
  const sum = field =>
    (items || []).reduce((total, item) => total + (item[field]?.count || 0), 0);

  return {
    outstanding: sum("outstanding"),
    suppressed: sum("suppressed"),
    reports: sum("reports")
  };
}

// Tones of the summary tiles.
export const TONES = {
  outstanding: "var(--color-soft-red)",
  suppressed: "var(--color-soft-green)",
  neutral: "rgb(var(--v-theme-primary))"
};
