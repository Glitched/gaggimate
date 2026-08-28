import { NumberInput } from './NumberInput.jsx';

/**
 * A labelled numeric input with a unit suffix, sized to what it holds.
 *
 * The profile form previously gave every field the full width of its grid
 * column, so a pressure of `3`, a temperature of `95` and a free-text profile
 * name all rendered ~450px wide. Field width is one of the few signals a form
 * has for how much input is expected, and spending it on nothing left every row
 * looking identical.
 *
 * Default width fits four characters plus a unit — enough for `100.5` — which is
 * more than any of these values needs. Pass `width` for the exceptions.
 *
 * Also collapses the form-control / label / input-group / label.input / span
 * nesting this pattern needed at every call site.
 */
export function NumberField({
  id,
  label,
  unit,
  unitLabel,
  value,
  onCommit,
  min,
  max,
  step,
  ariaLabel,
  width = 'w-32',
  className = '',
}) {
  return (
    <div className={`form-control ${className}`}>
      <label
        htmlFor={id}
        className='text-base-content/70 mb-2 block text-sm font-medium whitespace-nowrap'
      >
        {label}
      </label>
      <label htmlFor={id} className={`input ${width}`}>
        <NumberInput
          id={id}
          className='grow'
          value={value}
          onCommit={onCommit}
          min={min}
          max={max}
          step={step}
          aria-label={ariaLabel || label}
        />
        <span aria-label={unitLabel || unit}>{unit}</span>
      </label>
    </div>
  );
}
