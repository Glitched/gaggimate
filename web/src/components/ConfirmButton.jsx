import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { Tooltip } from './Tooltip.jsx';
import { useConfirmAction } from '../hooks/useConfirmAction.js';

// `label` renders the action's name beside the icon and, once armed, the
// confirm text -- needed anywhere the icon alone can't carry the two-step
// meaning, e.g. inside a dropdown menu. Without it the confirm state is only
// announced by a tooltip and the `hidden sm:inline` word below, so on a phone
// the first tap looks like nothing happened and the second one destroys data.
//
// The armed state disarms itself after a few seconds (useConfirmAction), so a
// stray first tap can't leave a destructive button primed indefinitely.
export function ConfirmButton({
  onAction,
  icon,
  tooltip,
  confirmTooltip,
  btnSize = 'sm',
  label = null,
  className = '',
}) {
  const { armed: confirm, armOrRun } = useConfirmAction(4000);

  return (
    <Tooltip content={confirm ? confirmTooltip : tooltip}>
      <button
        onClick={() => armOrRun(onAction)}
        className={`btn btn-ghost btn-${btnSize} text-error cursor-pointer transition-colors ${confirm ? 'bg-error text-error-content font-semibold' : 'hover:text-error hover:bg-error/10'} ${className}`}
        aria-label={confirm ? confirmTooltip : tooltip}
      >
        <FontAwesomeIcon icon={icon} />
        {label ? (
          <span className='ml-2'>{confirm ? confirmTooltip : label}</span>
        ) : (
          confirm && <span className='ml-2 hidden sm:inline'>Confirm</span>
        )}
      </button>
    </Tooltip>
  );
}
