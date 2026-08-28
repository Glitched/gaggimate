import { useEffect, useRef } from 'preact/hooks';
import { isTypingTarget } from './useHotkey.js';

/**
 * Vim/GitHub-style "/" to jump to a search field, and Escape to leave it.
 *
 * Returns a ref to put on the input:
 *
 *     const searchRef = useSearchHotkey();
 *     <input ref={searchRef} ... />
 *
 * The listener is on the document because the shortcut has to work while
 * nothing on the page is focused. It bows out whenever the keystroke could
 * plausibly mean something else:
 *
 * - the user is already typing (input/textarea/select/contenteditable), so "/"
 *   stays a literal slash in a text field or a profile name;
 * - a modifier is held, so browser and OS shortcuts are never shadowed.
 *
 * preventDefault matters: without it the "/" that triggered the jump lands in
 * the field we just focused, and every search starts with a stray slash.
 */
export function useSearchHotkey() {
  const ref = useRef(null);

  useEffect(() => {
    const onKeyDown = event => {
      const input = ref.current;
      if (!input) return;

      if (event.key === 'Escape' && document.activeElement === input) {
        input.blur();
        return;
      }

      if (event.key !== '/') return;
      if (event.ctrlKey || event.metaKey || event.altKey) return;
      if (isTypingTarget(event.target)) return;

      event.preventDefault();
      input.focus();
      input.select();
    };

    document.addEventListener('keydown', onKeyDown);
    return () => document.removeEventListener('keydown', onKeyDown);
  }, []);

  return ref;
}
