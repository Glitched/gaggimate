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

export function movePhase(phases, from, to) {
  if (from === to || from < 0 || to < 0 || from >= phases.length || to >= phases.length) {
    return phases;
  }
  const next = [...phases];
  const [moved] = next.splice(from, 1);
  next.splice(to, 0, moved);
  return next;
}
