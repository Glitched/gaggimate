export function getProfilePhases(data) {
  return Array.isArray(data?.phases) ? data.phases : [];
}

export function updatePhaseAt(phases, index, value) {
  const next = [...phases];
  next[index] = value;
  return next;
}

export function removePhaseAt(phases, index) {
  return phases.filter((_, i) => i !== index);
}

// Coerce a text-input value to a finite number. Falls back (default 0) when
// the field is empty or unparsable — e.g. mid-edit — and clamps to min/max,
// so profiles never store NaN or string numerics.
export function toFiniteNumber(value, fallback = 0, { min, max } = {}) {
  const n = typeof value === 'number' ? value : Number.parseFloat(value);
  if (!Number.isFinite(n)) return fallback;
  if (min !== undefined && n < min) return min;
  if (max !== undefined && n > max) return max;
  return n;
}

export function movePhase(phases, from, to) {
  if (from === to || from < 0 || to < 0 || from >= phases.length || to >= phases.length) {
    return phases;
  }
  const next = [...phases];
  const [moved] = next.splice(from, 1);
  next.splice(to, 0, moved);
  return next;
}
