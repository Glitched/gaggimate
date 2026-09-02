/** Brew ratio (out / in) to two decimals, or '' when either dose is missing or non-positive. */
export function calculateRatio(doseIn, doseOut) {
  if (doseIn && doseOut && Number.parseFloat(doseIn) > 0 && Number.parseFloat(doseOut) > 0) {
    return (Number.parseFloat(doseOut) / Number.parseFloat(doseIn)).toFixed(2);
  }
  return '';
}
