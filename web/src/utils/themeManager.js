const THEME_STORAGE_KEY = 'gaggimate-daisyui-theme';
const AVAILABLE_THEMES = ['system', 'light', 'dark', 'exhalation', 'coffee', 'nord'];
// What you get without ever opening the picker. Kept in step with the theme
// carrying `default: true` in style.css -- if the CSS default and this disagree,
// every load flashes one palette before settling on the other.
const DEFAULT_THEME = 'exhalation';

const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');

export function getSystemTheme() {
  return mediaQuery.matches ? 'dark' : 'light';
}

export function getStoredTheme() {
  try {
    const stored = localStorage.getItem(THEME_STORAGE_KEY);
    return stored && AVAILABLE_THEMES.includes(stored) ? stored : DEFAULT_THEME;
  } catch (error) {
    console.warn('Failed to get stored theme:', error);
    return DEFAULT_THEME;
  }
}

export function setStoredTheme(theme) {
  try {
    if (AVAILABLE_THEMES.includes(theme)) {
      localStorage.setItem(THEME_STORAGE_KEY, theme);
      applyTheme(theme);
      return true;
    }
    return false;
  } catch (error) {
    console.warn('Failed to set stored theme:', error);
    return false;
  }
}

export function applyTheme(theme) {
  if (!AVAILABLE_THEMES.includes(theme)) {
    return;
  }
  const actualTheme = theme === 'system' ? getSystemTheme() : theme;
  document.documentElement.setAttribute('data-theme', actualTheme);
}

export function getAvailableThemes() {
  return AVAILABLE_THEMES.map(theme => ({
    value: theme,
    label: theme.charAt(0).toUpperCase() + theme.slice(1),
  }));
}

function onSystemThemeChange() {
  if (getStoredTheme() === 'system') {
    applyTheme('system');
  }
}

// Initialize theme on load
export function initializeTheme() {
  const theme = getStoredTheme();
  mediaQuery.addEventListener('change', onSystemThemeChange);
  applyTheme(theme);
}

// Simple function to handle theme change from select element
export function handleThemeChange(event) {
  const theme = event.target.value;
  setStoredTheme(theme);
}
