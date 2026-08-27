import { useCallback, useEffect, useRef } from 'preact/hooks';

/**
 * usePressRepeat
 * Press-and-hold auto-repeat for +/- stepper buttons: fires once on pointer
 * down, then repeats while held. Keyboard activation (Enter/Space) still
 * works through the click event, which reports detail === 0 for keyboard.
 *
 * Spread the returned props onto a <button>.
 */
export function usePressRepeat(callback, { initialDelayMs = 450, intervalMs = 150 } = {}) {
  const callbackRef = useRef(callback);
  callbackRef.current = callback;
  const delayTimerRef = useRef(null);
  const repeatTimerRef = useRef(null);

  const stop = useCallback(() => {
    clearTimeout(delayTimerRef.current);
    clearInterval(repeatTimerRef.current);
    delayTimerRef.current = null;
    repeatTimerRef.current = null;
  }, []);

  // Stop repeating if the button unmounts mid-press.
  useEffect(() => stop, [stop]);

  const onPointerDown = useCallback(
    event => {
      if (event.pointerType === 'mouse' && event.button !== 0) return;
      callbackRef.current?.();
      stop();
      delayTimerRef.current = setTimeout(() => {
        repeatTimerRef.current = setInterval(() => callbackRef.current?.(), intervalMs);
      }, initialDelayMs);
    },
    [initialDelayMs, intervalMs, stop],
  );

  const onClick = useCallback(event => {
    // Pointer presses already fired on pointerdown; their trailing click has
    // detail >= 1. Keyboard-triggered clicks have detail === 0 — let those
    // through so the buttons stay keyboard accessible.
    if (event.detail === 0) {
      callbackRef.current?.();
    }
  }, []);

  // A long-press must not open the context menu on mobile.
  const onContextMenu = useCallback(event => event.preventDefault(), []);

  return {
    onPointerDown,
    onPointerUp: stop,
    onPointerLeave: stop,
    onPointerCancel: stop,
    onClick,
    onContextMenu,
    style: { touchAction: 'manipulation' },
  };
}
