// Web UI v2: replaces src/plugins/vuetify.js (see vite.config.mjs).

import "@fontsource-variable/inter";
import "vuetify/styles";
import "@v2/styles/main.scss";

import { createVuetify } from "vuetify";
import * as components from "vuetify/components";
import * as directives from "vuetify/directives";
import { aliases, mdi } from "vuetify/iconsets/mdi";

import { initialTheme } from "@v2/composables/useThemeMode";

import {
  ArcElement,
  Chart,
  Legend,
  PieController,
  Tooltip,
  defaults as chartDefaults
} from "chart.js";

// Charts in the style of the page. Registering a Chart.js plugin merges its
// defaults again, so the plugins are registered before their defaults are
// changed; later registrations of the same plugins change nothing.
Chart.register(ArcElement, Legend, PieController, Tooltip);
chartDefaults.font.family = "\"Inter Variable\", system-ui, sans-serif";
chartDefaults.color = "#64748b";
chartDefaults.borderColor = "rgba(100, 116, 139, .16)";
Object.assign(chartDefaults.plugins.legend.labels, {
  usePointStyle: true,
  pointStyle: "circle",
  boxWidth: 8,
  boxHeight: 8,
  padding: 16
});
// Pies are donuts, with gaps instead of white borders between the slices.
Chart.overrides.pie.cutout = "60%";
Object.assign(chartDefaults.elements.arc, { borderWidth: 0, spacing: 3 });
Object.assign(chartDefaults.plugins.tooltip, {
  backgroundColor: "#0f172a",
  padding: 10,
  cornerRadius: 8,
  boxPadding: 4,
  usePointStyle: true
});

// Shared by both themes: the status colors keep their meaning.
const statusColors = {
  error: "#e11d48",
  warning: "#f59e0b",
  info: "#0ea5e9",
  success: "#10b981"
};

export default createVuetify({
  components,
  directives,
  icons: {
    defaultSet: "mdi",
    aliases,
    sets: {
      mdi,
    },
  },
  theme: {
    defaultTheme: initialTheme(),
    themes: {
      light: {
        dark: false,
        colors: {
          ...statusColors,
          background: "#f5f6fb",
          surface: "#ffffff",
          "surface-variant": "#eef0f7",
          "on-surface-variant": "#1e2235",
          primary: "#1d4ed8",
          secondary: "#1e40af",
          accent: "#0284c7",
          grey: "#94a3b8",
          "grey-card": "#eef0f7"
        },
        variables: {
          "border-color": "#1e2235",
          "border-opacity": 0.09,
          "hover-opacity": 0.05,
          "theme-code": "#eef0f7",
          "theme-on-code": "#1e2235"
        }
      },
      dark: {
        dark: true,
        colors: {
          ...statusColors,
          background: "#0b1020",
          surface: "#121a2e",
          "surface-bright": "#1a2440",
          "surface-variant": "#1e2944",
          "on-surface-variant": "#e2e8f0",
          primary: "#60a5fa",
          secondary: "#3b82f6",
          accent: "#38bdf8",
          grey: "#64748b",
          "grey-card": "#1e2944"
        },
        variables: {
          "border-color": "#bfdbfe",
          "border-opacity": 0.1,
          "hover-opacity": 0.06,
          "theme-code": "#1e2944",
          "theme-on-code": "#e2e8f0"
        }
      }
    }
  },
  defaults: {
    VBtn: {
      rounded: "lg"
    },
    VCard: {
      density: "comfortable",
      rounded: "xl"
    },
    VChip: {
      rounded: "pill"
    },
    VDialog: {
      transition: "cc-dialog-transition"
    },
    VMenu: {
      transition: "cc-menu-transition"
    },
    VTable: {
      density: "compact",
    },
    VDataTableServer: {
      density: "compact",
    },
    VTooltip: {
      transition: "cc-menu-transition"
    }
  }
});
