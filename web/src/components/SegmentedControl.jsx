/**
 * A row of mutually exclusive options.
 *
 * Replaces the `join` + `btn-primary`/`btn-outline` pairing this form used in
 * four places. That pairing gave every unselected option a full-strength border
 * — a dozen bright rectangles on a near-black page — and marked the selected one
 * with a solid fill, so each group shouted twice: once with outlines, once with
 * a block of colour.
 *
 * Here the group is a single recessed track with no internal borders, and
 * selection is a tint, matching the active row in the sidebar and the current
 * page in the pagination.
 *
 * Radii are concentric: the track is rounded-xl (12px) with p-1 (4px), so items
 * are rounded-lg (8px) = 12 − 4.
 *
 * @param {string}   [label]      Rendered as a <legend> above the group.
 * @param {any}      value        Currently selected option value.
 * @param {(v:any)=>void} onChange Called with the option's value. Not called
 *                                when the option is already selected.
 * @param {Array<{value:any,label:string,ariaLabel?:string}>} options
 *                                Build this with conditionals in the caller
 *                                rather than conditionally rendering children.
 * @param {string}   [ariaLabel]  Group label when `label` is absent.
 * @param {string}   [className]
 */
export function SegmentedControl({ label, value, onChange, options, ariaLabel, className = '' }) {
  const group = (
    <div
      role='group'
      aria-label={ariaLabel || label}
      // flex-wrap rather than a vertical stack on small screens: six pump
      // modes wrap to two tidy rows instead of becoming a six-high column.
      className={`bg-base-200 inline-flex flex-wrap gap-1 rounded-xl p-1 ${className}`}
    >
      {options.map(option => {
        const selected = option.value === value;
        return (
          <button
            key={String(option.value)}
            type='button'
            onClick={() => !selected && onChange(option.value)}
            aria-pressed={selected}
            aria-label={option.ariaLabel || option.label}
            className={`cursor-pointer rounded-lg px-3 py-1.5 text-sm transition-colors ${
              selected
                ? 'bg-primary/15 text-primary font-medium'
                : 'text-base-content/60 hover:text-base-content hover:bg-base-content/10'
            }`}
          >
            {option.label}
          </button>
        );
      })}
    </div>
  );

  if (!label) return group;

  return (
    <fieldset>
      <legend className='text-base-content/70 mb-2 block text-sm font-medium'>{label}</legend>
      {group}
    </fieldset>
  );
}
