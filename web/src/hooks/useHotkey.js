import { useEffect } from 'preact/hooks';

/**
 * True when the keystroke belongs to whatever the user is currently typing in.
 *
 * Every global shortcut has to agree on this, or one of them will eat a
 * character somebody meant to type into a profile name. Shared so the rule is
 * defined once.
 */
export function isTypingTarget(element) {
  if (!element) return false;
  if (element.isContentEditable) return true;
  const tag = element.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
}

/**
 * Bind a single-key shortcut on the document.
 *
 * Fires only for the bare key: no Ctrl/Cmd/Alt, so browser and OS shortcuts are
 * never shadowed (Ctrl+B stays "bold" / the browser's bookmark bar), and not
 * while the user is typing.
 *
 * @param {string} key           `KeyboardEvent.key`, compared case-insensitively.
 * @param {(event: KeyboardEvent) => void} handler
 * @param {boolean} [enabled]    Set false to unbind without changing call order.
 */
export function useHotkey(key, handler, enabled = true) {
  useEffect(() => {
    if (!enabled) return undefined;

    const onKeyDown = event => {
      if (event.key?.toLowerCase() !== key.toLowerCase()) return;
      if (event.ctrlKey || event.metaKey || event.altKey) return;
      if (isTypingTarget(event.target)) return;

      // We are claiming this key, so stop anything the browser would
      // otherwise do with it (Firefox's type-ahead find, for one).
      event.preventDefault();
      handler(event);
    };

    document.addEventListener('keydown', onKeyDown);
    return () => document.removeEventListener('keydown', onKeyDown);
  }, [key, handler, enabled]);
}
