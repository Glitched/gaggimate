import Card from '../../components/Card.jsx';
import { Spinner } from '../../components/Spinner.jsx';
import { isNumber } from 'chart.js/helpers';
import { faChevronDown } from '@fortawesome/free-solid-svg-icons/faChevronDown';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faArrowUp } from '@fortawesome/free-solid-svg-icons/faArrowUp';
import { faArrowDown } from '@fortawesome/free-solid-svg-icons/faArrowDown';
import { Tooltip } from '../../components/Tooltip.jsx';
import { ProfileMainInformation } from './ProfileMainInformation.jsx';
import { SegmentedControl } from '../../components/SegmentedControl.jsx';
import { NumberInput } from '../../components/NumberInput.jsx';
import { getProfilePhases, movePhase, removePhaseAt, updatePhaseAt } from './profilePhases.js';

export function StandardProfileForm(props) {
  const { data, onChange, onSave, saving = true, pressureAvailable = false } = props;
  const phases = getProfilePhases(data);

  // Each mode writes a different payload; carried over from the individual
  // button handlers so SegmentedControl can stay value-based.
  const selectPumpMode = next => {
    if (next === mode) return;
    if (next === 'off') return onFieldChange('pump', 0);
    if (next === 'power') return onFieldChange('pump', 100);
    const pressure = phase.pump?.pressure || 0;
    const flow = phase.pump?.flow || 0;
    if (next === 'pressure') return onFieldChange('pump', { target: 'pressure', pressure, flow });
    if (next === 'flow') return onFieldChange('pump', { target: 'flow', pressure, flow });
  };

  const onFieldChange = (field, value) => {
    onChange({
      ...data,
      [field]: value,
    });
  };

  const onPhaseChange = (index, value) => {
    onChange({
      ...data,
      phases: updatePhaseAt(phases, index, value),
    });
  };

  const onPhaseAdd = () => {
    onChange({
      ...data,
      phases: [
        ...phases,
        {
          phase: 'brew',
          name: 'New Phase',
          pump: 100,
          valve: 1,
          duration: 0,
          targets: [],
        },
      ],
    });
  };

  const onPhaseRemove = index => {
    onChange({
      ...data,
      phases: removePhaseAt(phases, index),
    });
  };

  const onPhaseMove = (index, direction) => {
    onChange({
      ...data,
      phases: movePhase(phases, index, index + direction),
    });
  };

  return (
    <form
      onSubmit={e => {
        e.preventDefault();
        onSave(data);
      }}
    >
      <div className='grid grid-cols-1 gap-4 lg:grid-cols-10'>
        <ProfileMainInformation
          data={data}
          onChangeLabel={e => onFieldChange('label', e.target.value)}
          onChangeDescription={e => onFieldChange('description', e.target.value)}
          onCommitTemperature={value => onFieldChange('temperature', value)}
          onChangeUtility={e => onFieldChange('utility', !!e.target.checked)}
        />

        <Card sm={10} title='Brew Phases'>
          <div className='space-y-4' role='group' aria-label='Brew phases configuration'>
            {phases.map((value, index) => (
              <div key={index}>
                {index > 0 && (
                  <div className='flex flex-col items-center py-2' aria-hidden='true'>
                    <FontAwesomeIcon
                      icon={faChevronDown}
                      className='text-base-content/60 text-lg'
                    />
                  </div>
                )}
                <Phase
                  phase={value}
                  index={index}
                  onChange={phase => onPhaseChange(index, phase)}
                  onRemove={() => onPhaseRemove(index)}
                  onMoveUp={() => onPhaseMove(index, -1)}
                  onMoveDown={() => onPhaseMove(index, 1)}
                  isFirst={index === 0}
                  isLast={index === phases.length - 1}
                  pressureAvailable={pressureAvailable}
                />
              </div>
            ))}
            <div className='flex flex-row justify-center pt-4'>
              <button
                type='button'
                className='btn btn-ghost text-base-content/70 hover:text-base-content hover:bg-base-content/10 gap-2 border-none'
                onClick={onPhaseAdd}
                aria-label='Add new brew phase'
              >
                <FontAwesomeIcon icon={faPlus} />
                <span>Add phase</span>
              </button>
            </div>
          </div>
        </Card>
      </div>

      <div className='pt-4 pb-8 lg:col-span-10'>
        <div className='flex flex-col gap-2 sm:flex-row'>
          <a
            href='/profiles'
            className='btn btn-ghost text-base-content/70 hover:text-base-content hover:bg-base-content/10 border-none'
          >
            Back
          </a>
          <button
            type='submit'
            className='btn btn-primary gap-2'
            disabled={saving}
            aria-label={saving ? 'Saving profile...' : 'Save profile'}
          >
            <span>Save</span>
            {saving && <Spinner size={4} />}
          </button>
        </div>
      </div>
    </form>
  );
}

function Phase({
  phase,
  index,
  onChange,
  onRemove,
  onMoveUp,
  onMoveDown,
  isFirst,
  isLast,
  pressureAvailable,
}) {
  const onFieldChange = (field, value) => {
    onChange({
      ...phase,
      [field]: value,
    });
  };

  const onVolumetricTargetChange = value => {
    if (!Number.isFinite(value) || value <= 0) {
      onChange({
        ...phase,
        targets: [],
      });
      return;
    }
    onChange({
      ...phase,
      targets: [
        {
          type: 'volumetric',
          value: value,
        },
      ],
    });
  };

  const targets = phase?.targets || [];
  const volumetricTarget = targets.find(t => t.type === 'volumetric') || {};
  const targetWeight = volumetricTarget?.value || 0;

  // Imported profiles can lack `pump` entirely — treat that as full power
  // instead of crashing on `undefined.pressure`.
  const pump = phase.pump ?? 100;
  const pumpPower = isNumber(pump) ? pump : 100;
  const pressure = !isNumber(pump) ? pump.pressure : 0;
  const flow = !isNumber(pump) ? pump.flow : 0;
  const mode = isNumber(pump) ? (pump === 0 ? 'off' : 'power') : pump.target;

  return (
    <div
      className='bg-base-200 border-base-300 space-y-4 rounded-lg border p-4'
      role='group'
      aria-label={`Phase ${index + 1} configuration`}
    >
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-type`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Phase Type
          </label>
          <select
            id={`phase-${index}-type`}
            className='select select-bordered w-full'
            onChange={e => onFieldChange('phase', e.target.value)}
            value={phase.phase}
            aria-label='Select the type of brew phase'
          >
            <option value='preinfusion'>Pre-Infusion</option>
            <option value='brew'>Brew</option>
          </select>
        </div>
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-name`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Phase Name
          </label>
          <div className='flex gap-2'>
            <input
              id={`phase-${index}-name`}
              className='input input-bordered flex-1'
              placeholder='Name...'
              value={phase.name}
              onChange={e => onFieldChange('name', e.target.value)}
              aria-label='Enter a name for this phase'
            />
            <Tooltip content='Move phase earlier'>
              <button
                type='button'
                onClick={onMoveUp}
                disabled={isFirst}
                className='btn btn-sm btn-ghost'
                aria-label={`Move phase ${index + 1} earlier`}
              >
                <FontAwesomeIcon icon={faArrowUp} />
              </button>
            </Tooltip>
            <Tooltip content='Move phase later'>
              <button
                type='button'
                onClick={onMoveDown}
                disabled={isLast}
                className='btn btn-sm btn-ghost'
                aria-label={`Move phase ${index + 1} later`}
              >
                <FontAwesomeIcon icon={faArrowDown} />
              </button>
            </Tooltip>
            <Tooltip content='Delete this phase'>
              <button
                type='button'
                onClick={onRemove}
                className='btn btn-sm btn-ghost text-error'
                aria-label={`Delete phase ${index + 1}`}
              >
                <FontAwesomeIcon icon={faTrashCan} />
              </button>
            </Tooltip>
          </div>
        </div>
      </div>

      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-duration`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Duration
          </label>
          <div className='input-group'>
            <label htmlFor={`phase-${index}-duration`} className='input w-full'>
              <NumberInput
                id={`phase-${index}-duration`}
                className='grow'
                min={0}
                value={phase.duration}
                onCommit={value => onFieldChange('duration', value)}
                aria-label='Duration in seconds'
              />
              <span aria-label='seconds'>s</span>
            </label>
          </div>
        </div>
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-target`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Volumetric Target
          </label>
          <div className='input-group'>
            <label htmlFor={`phase-${index}-target`} className='input w-full'>
              <NumberInput
                id={`phase-${index}-target`}
                className='grow'
                value={targetWeight}
                onCommit={onVolumetricTargetChange}
                aria-label='Target weight in grams'
                min={0}
                step='0.1'
              />
              <span aria-label='grams'>g</span>
            </label>
          </div>
        </div>
      </div>

      <div className='form-control'>
        <SegmentedControl
          label='Valve'
          ariaLabel='Valve state selection'
          value={phase.valve ? 'open' : 'closed'}
          onChange={next => onFieldChange('valve', next === 'open' ? 1 : 0)}
          options={[
            { value: 'closed', label: 'Closed', ariaLabel: 'Valve closed' },
            { value: 'open', label: 'Open', ariaLabel: 'Valve open' },
          ]}
        />
      </div>

      <div className='form-control'>
        <SegmentedControl
          label='Pump Mode'
          ariaLabel='Pump mode selection'
          value={mode}
          onChange={selectPumpMode}
          options={[
            { value: 'off', label: 'Off', ariaLabel: 'Pump off' },
            { value: 'power', label: 'Power', ariaLabel: 'Pump power mode' },
            ...(pressureAvailable
              ? [
                  {
                    value: 'pressure',
                    label: (
                      <>
                        Pressure <sup>PRO</sup>
                      </>
                    ),
                    ariaLabel: 'Pump pressure mode (PRO feature)',
                  },
                  {
                    value: 'flow',
                    label: (
                      <>
                        Flow <sup>PRO</sup>
                      </>
                    ),
                    ariaLabel: 'Pump flow mode (PRO feature)',
                  },
                ]
              : []),
          ]}
        />
      </div>

      {mode === 'power' && (
        <div className='form-control'>
          <label
            htmlFor={`phase-${index}-power`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Pump Power
          </label>
          <div className='input-group'>
            <label htmlFor={`phase-${index}-power`} className='input w-full'>
              <NumberInput
                id={`phase-${index}-power`}
                className='grow'
                step='1'
                min={0}
                max={100}
                value={pumpPower}
                onCommit={value => onFieldChange('pump', value)}
                aria-label='Pump power as percentage'
              />
              <span aria-label='percent'>%</span>
            </label>
          </div>
        </div>
      )}

      {(mode === 'pressure' || mode === 'flow') && (
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          <div className='form-control'>
            <label
              htmlFor={`phase-${index}-pressure`}
              className='text-base-content/70 mb-2 block text-sm font-medium'
            >
              Pressure {mode === 'pressure' ? 'Target' : 'Limit'}
            </label>
            <div className='input-group'>
              <label htmlFor={`phase-${index}-pressure`} className='input w-full'>
                <NumberInput
                  id={`phase-${index}-pressure`}
                  className='grow'
                  step='0.01'
                  value={pressure}
                  onCommit={value =>
                    onFieldChange('pump', {
                      ...phase.pump,
                      pressure: value,
                    })
                  }
                  aria-label='Pressure in bar'
                  min={0}
                />
                <span aria-label='bar'>bar</span>
              </label>
            </div>
          </div>
          <div className='form-control'>
            <label
              htmlFor={`phase-${index}-flow`}
              className='text-base-content/70 mb-2 block text-sm font-medium'
            >
              Flow {mode === 'flow' ? 'Target' : 'Limit'}
            </label>
            <div className='input-group'>
              <label htmlFor={`phase-${index}-flow`} className='input w-full'>
                <NumberInput
                  id={`phase-${index}-flow`}
                  className='grow'
                  step='0.01'
                  value={flow}
                  onCommit={value =>
                    onFieldChange('pump', {
                      ...phase.pump,
                      flow: value,
                    })
                  }
                  aria-label='Flow rate in grams per second'
                  min={0}
                />
                <span aria-label='grams per second'>g/s</span>
              </label>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
