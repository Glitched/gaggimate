import { useEffect } from 'preact/hooks';

/**
 * useUnsavedChangesGuard
 * Warns before the user's unsaved edits are lost:
 * - tab close / reload via `beforeunload`
 * - in-app navigation by intercepting anchor clicks in the capture phase,
 *   before preact-iso's router sees them, and asking for confirmation.
 *
 * Programmatic navigation (location.route after a successful save) is
 * deliberately NOT intercepted.
 *
 * @param {boolean} isDirty - Whether there are unsaved edits.
 * @param {object} [options]
 * @param {string} [options.message] - Confirmation prompt text.
 * @param {string} [options.ignorePrefix] - Path prefix to let through without
 *   confirmation (e.g. '/settings' so switching settings tabs, which keeps
 *   the form mounted and its state intact, doesn't nag).
 */
export function useUnsavedChangesGuard(isDirty, { message, ignorePrefix } = {}) {
  const promptText = message || 'You have unsaved changes. Leave without saving?';

  useEffect(() => {
    if (!isDirty) return;

    const onBeforeUnload = event => {
      event.preventDefault();
      // Required by some browsers for the prompt to appear.
      event.returnValue = '';
    };

    const onClickCapture = event => {
      if (event.defaultPrevented || event.button !== 0) return;
      if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
      const anchor = event.target instanceof Element ? event.target.closest('a[href]') : null;
      if (!anchor || anchor.target === '_blank') return;
      const href = anchor.getAttribute('href');
      if (!href || !href.startsWith('/')) return;
      if (ignorePrefix && href.startsWith(ignorePrefix)) return;
      if (!confirm(promptText)) {
        event.preventDefault();
        event.stopImmediatePropagation();
      }
    };

    window.addEventListener('beforeunload', onBeforeUnload);
    document.addEventListener('click', onClickCapture, true);
    return () => {
      window.removeEventListener('beforeunload', onBeforeUnload);
      document.removeEventListener('click', onClickCapture, true);
    };
  }, [isDirty, promptText, ignorePrefix]);
}
