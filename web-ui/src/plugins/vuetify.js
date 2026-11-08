import "vuetify/styles";
import { createVuetify } from "vuetify";
import * as components from "vuetify/components";
import * as directives from "vuetify/directives";
import { aliases, mdi } from "vuetify/iconsets/mdi";

// The web UI v2 build replaces this file with src-v2/plugins/vuetify.js.
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
    defaultTheme: "light",
    themes: {
      light: {
        colors: {
          primary: "#2280c3",
          secondary: "#2c87c7",
          accent: "#009688",
          error: "#f44336",
          warning: "#ff9800",
          info: "#3f51b5",
          success: "#4caf50",
          grey: "#9E9E9E",
          "grey-card": "#e0e0e0"
        }
      }
    }
  },
  defaults: {
    VCard: {
      density: "comfortable",
    },
    VTable: {
      density: "compact",
    },
    VDataTableServer: {
      density: "compact",
    }
  }
});
