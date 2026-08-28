import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { NumberInput } from '../../components/NumberInput.jsx';

export const TargetTypes = [
  {
    label: 'Water drawn',
    type: 'pumped',
    operator: 'gte',
    unit: 'ml',
  },
  {
    label: 'Weight reached',
    type: 'volumetric',
    operator: 'gte',
    unit: 'g',
  },
  {
    label: 'Pressure above',
    type: 'pressure',
    operator: 'gte',
    unit: 'bar',
  },
  {
    label: 'Pressure below',
    type: 'pressure',
    operator: 'lte',
    unit: 'bar',
  },
  {
    label: 'Flow above',
    type: 'flow',
    operator: 'gte',
    unit: 'ml/s',
  },
  {
    label: 'Flow below',
    type: 'flow',
    operator: 'lte',
    unit: 'ml/s',
  },
];

export function ExtendedPhaseTarget({ onChange, target, index, onRemove }) {
  const targetType =
    TargetTypes.find(tt => tt.type === target.type && tt.operator === (target.operator || 'gte')) ||
    TargetTypes[0];
  return (
    <>
      {/* The value is two or three digits, so the field is sized for that rather
          than spanning the card, and the remove control sits beside it. */}
      <div className='flex flex-wrap items-end gap-2'>
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-target-value`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            {targetType.label}
          </label>
          <div className='flex flex-row items-center gap-1'>
            <div className='input-group'>
              <label htmlFor={`phase-${index}-target-value`} className='input w-32'>
                {/* The previous code called toFiniteNumber without importing
                    it — editing a target value threw a ReferenceError. */}
                <NumberInput
                  id={`phase-${index}-target-value`}
                  className='grow'
                  value={target.value || 0}
                  onCommit={value =>
                    onChange({
                      ...target,
                      value,
                    })
                  }
                  aria-label={`Target value in ${targetType.unit}`}
                  min={0}
                  step='0.1'
                />
                <span aria-label={targetType.unit}>{targetType.unit}</span>
              </label>
            </div>
            <button
              type='button'
              className='text-error/70 hover:text-error hover:bg-error/10 cursor-pointer rounded-lg px-3 py-2 transition-colors'
              aria-label='Remove target'
              onClick={() => onRemove()}
            >
              <FontAwesomeIcon icon={faTrashCan} />
            </button>
          </div>
        </div>
      </div>
    </>
  );
}
