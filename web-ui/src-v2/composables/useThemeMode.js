import { computed } from "vue";
import { useTheme } from "vuetify";

// splash.html reads the same key to pick the theme before the app loads.
const STORAGE_KEY = "cc-web-ui-v2-theme";

function storedTheme() {
  try {
    const theme = localStorage.getItem(STORAGE_KEY);
    return theme === "light" || theme === "dark" ? theme : null;
  } catch {
    return null;
  }
}

// The theme to start with: the one the user chose last, otherwise the
// color scheme of the operating system.
export function initialTheme() {
  const theme = storedTheme();
  if (theme) return theme;

  return window.matchMedia?.("(prefers-color-scheme: dark)").matches
    ? "dark" : "light";
}

export function useThemeMode() {
  const theme = useTheme();

  const isDark = computed(() => theme.global.current.value.dark);

  function setTheme(name) {
    theme.change(name);
    document.documentElement.setAttribute("data-cc-theme", name);
    try {
      localStorage.setItem(STORAGE_KEY, name);
    } catch {
      // Without storage the choice lasts until the page is reloaded.
    }
  }

  function toggleTheme() {
    setTheme(isDark.value ? "light" : "dark");
  }

  return { isDark, setTheme, toggleTheme };
}
