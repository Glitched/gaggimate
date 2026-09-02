// Resolves the app's active daisyUI theme (the data-theme attribute that
// themeManager keeps on <html>) to chart colors. Charts previously keyed off
// the OS prefers-color-scheme or hardcoded a dark palette, so a user on the
// 'coffee' theme with a light OS (or vice versa) got mismatched charts.

// Matches the color-scheme declared per theme in style.css.
const DARK_THEMES = new Set(['dark', 'exhalation', 'coffee']);

export function isDarkAppTheme() {
  return DARK_THEMES.has(document.documentElement.getAttribute('data-theme'));
}

// Colors for the phase-marker label pills drawn by chartjs-plugin-annotation,
// following the two styles the profile chart already used for dark/light.
export function getChartAnnotationLabelColors() {
  const dark = isDarkAppTheme();
  return {
    color: dark ? 'rgb(255,255,255)' : 'rgb(0,0,0)',
    backgroundColor: dark ? 'rgba(22,33,50,0.75)' : 'rgba(255,255,255,0.75)',
  };
}
