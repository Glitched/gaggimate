import { signal } from '@preact/signals';

// Minimal app-wide toast store. Pages call showToast() instead of alert();
// the <ToastHost /> in the app shell renders whatever is queued.

let nextToastId = 0;

export const toasts = signal([]);

const DEFAULT_DURATION_MS = 4000;

export function showToast(message, { type = 'info', duration = DEFAULT_DURATION_MS } = {}) {
  const id = ++nextToastId;
  toasts.value = [...toasts.value, { id, message, type }];
  setTimeout(() => dismissToast(id), duration);
  return id;
}

export function dismissToast(id) {
  if (!toasts.value.some(t => t.id === id)) return;
  toasts.value = toasts.value.filter(t => t.id !== id);
}
