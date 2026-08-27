import { useState } from 'preact/hooks';

// Controlled number input that tolerates transient edit states. The previous
// pattern clamped on every keystroke, so clearing a field to retype instantly
// snapped it to 0 and select-all-and-type was impossible. While this field has
// focus the raw text (including '') lives in local state; valid numbers still
// commit live so previews stay in sync, and the clamped canonical value is
// committed on blur.
export function NumberInput({ value, onCommit, fallback = 0, min, max, ...inputProps }) {
  const [draft, setDraft] = useState(null);

  const clamp = raw => {
    const n = typeof raw === 'number' ? raw : Number.parseFloat(raw);
    if (!Number.isFinite(n)) return fallback;
    if (min !== undefined && n < min) return min;
    if (max !== undefined && n > max) return max;
    return n;
  };

  return (
    <input
      type='number'
      {...inputProps}
      min={min}
      max={max}
      value={draft ?? value}
      onInput={e => {
        const raw = e.currentTarget.value;
        setDraft(raw);
        const n = Number.parseFloat(raw);
        if (Number.isFinite(n)) onCommit(clamp(n));
      }}
      onBlur={e => {
        setDraft(null);
        onCommit(clamp(e.currentTarget.value));
      }}
    />
  );
}
