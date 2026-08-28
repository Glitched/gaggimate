import { ExtendedPhaseTarget, TargetTypes } from './ExtendedPhaseTarget.jsx';
import { isNumber } from 'chart.js/helpers';
import { useCallback } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { Tooltip } from '../../components/Tooltip.jsx';
import { SegmentedControl } from '../../components/SegmentedControl.jsx';
import { NumberInput } from '../../components/NumberInput.jsx';

export function ExtendedPhase({ phase, index, onChange, onRemove, pressureAvailable }) {
  const onFieldChange = (field, value) => {
    onChange({
      ...phase,
      [field]: value,
    });
  };

  const onTargetChange = (index, value) => {
    // Replace the array too — the shallow copy shares `targets` with the
    // previous state object, so writing into it would mutate parent state.
    onChange({
      ...phase,
      targets: (phase.targets || []).map((t, i) => (i === index ? value : t)),
    });
  };

  const onTargetRemove = index => {
    const newPhase = {
      ...phase,
      targets: [],
    };
    for (let i = 0; i < phase.targets.length; i++) {
      if (i !== index) {
        newPhase.targets.push(phase.targets[i]);
      }
    }
    onChange(newPhase);
  };

  const onTargetAdd = target => {
    onChange({
      ...phase,
      targets: [...(phase.targets || []), target],
    });
  };

  const targets = phase?.targets || [];

  // Imported profiles can lack `pump` entirely — treat that as full power
  // instead of crashing on `undefined.pressure`.
  const pump = phase.pump ?? 100;
  const pumpPower = isNumber(pump) ? pump : 100;
  const pressure = !isNumber(pump) ? pump.pressure : 0;
  const flow = !isNumber(pump) ? pump.flow : 0;
  let mode = isNumber(pump) ? (pump === 0 ? 'off' : 'power') : pump.target;
  if (mode === 'pressure' && pressure === -1) mode = 'hold-pressure';
  if (mode === 'flow' && flow === -1) mode = 'hold-flow';
  const availableTargetTypes = TargetTypes.filter(
    t => !targets.find(t2 => t2.type === t.type && t2.operator === t.operator),
  );

  // Each pump mode writes a different payload; this carries what the individual
  // button handlers used to do so SegmentedControl can stay value-based.
  const selectPumpMode = next => {
    if (next === mode) return;
    if (next === 'off') return onFieldChange('pump', 0);
    if (next === 'power') return onFieldChange('pump', 100);
    const pressure = Math.max(phase.pump?.pressure || 0, 0);
    const flow = Math.max(phase.pump?.flow || 0, 0);
    // -1 means "hold whatever the puck gives", hence the Maintain modes.
    if (next === 'pressure') return onFieldChange('pump', { target: 'pressure', pressure, flow });
    if (next === 'flow') return onFieldChange('pump', { target: 'flow', pressure, flow });
    if (next === 'hold-pressure')
      return onFieldChange('pump', { target: 'pressure', pressure: -1, flow });
    if (next === 'hold-flow') return onFieldChange('pump', { target: 'flow', pressure, flow: -1 });
  };

  let rampUnit = 's';
  if (phase.transition?.target === 'volumetric') rampUnit = 'g';
  if (phase.transition?.target === 'pumped') rampUnit = 'ml';

  return (
    <div
      className='grid grid-cols-1 gap-2 p-2'
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
          <SegmentedControl
            value={phase.phase}
            onChange={next => onFieldChange('phase', next)}
            ariaLabel='Select the type of brew phase'
            options={[
              { value: 'preinfusion', label: 'Pre-Infusion' },
              { value: 'brew', label: 'Brew' },
            ]}
          />
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
            htmlFor={`phase-${index}-temperature`}
            className='text-base-content/70 mb-2 block text-sm font-medium'
          >
            Temperature (0 = Default)
          </label>
          <div className='input-group'>
            <label htmlFor={`phase-${index}-temperature`} className='input w-full'>
              <NumberInput
                id={`phase-${index}-temperature`}
                className='grow'
                value={phase.temperature || 0}
                onCommit={value => onFieldChange('temperature', value)}
                aria-label='Target temperature'
                min={0}
                step='0.1'
              />
              <span aria-label='celsius'>°C</span>
            </label>
          </div>
        </div>
      </div>

      <div className='form-control'>
        <SegmentedControl
          label='Valve'
          ariaLabel='Valve state selection'
          value={phase.valve ? 'open' : 'closed'}
          onChange={next => onFieldChange('valve', next === 'open')}
          options={[
            { value: 'closed', label: 'Closed' },
            { value: 'open', label: 'Open' },
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
                  { value: 'pressure', label: 'Pressure', ariaLabel: 'Pump pressure mode' },
                  { value: 'flow', label: 'Flow', ariaLabel: 'Pump flow mode' },
                  {
                    value: 'hold-pressure',
                    label: 'Maintain Pressure',
                    ariaLabel: 'Pump maintain pressure mode',
                  },
                  {
                    value: 'hold-flow',
                    label: 'Maintain Flow',
                    ariaLabel: 'Pump maintain flow mode',
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

      {(mode === 'pressure' ||
        mode === 'flow' ||
        mode === 'hold-pressure' ||
        mode === 'hold-flow') && (
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          {mode !== 'hold-pressure' && (
            <div className='form-control'>
              <label
                htmlFor={`phase-${index}-pressure`}
                className='text-base-content/70 mb-2 block text-sm font-medium'
              >
                {mode === 'pressure' ? 'Target' : 'Maximum'} Pressure{' '}
                {mode === 'flow' && '(0 = Ignore)'}
              </label>
              <div className='input-group'>
                <label htmlFor={`phase-${index}-pressure`} className='input w-full'>
                  <NumberInput
                    id={`phase-${index}-pressure`}
                    className='grow'
                    step='0.01'
                    min={mode === 'pressure' ? 0.1 : 0}
                    value={pressure}
                    onCommit={value =>
                      onFieldChange('pump', {
                        ...phase.pump,
                        pressure: value,
                      })
                    }
                    aria-label='Pressure in bar'
                  />
                  <span aria-label='bar'>bar</span>
                </label>
              </div>
            </div>
          )}
          {mode !== 'hold-flow' && (
            <div className='form-control'>
              <label
                htmlFor={`phase-${index}-flow`}
                className='text-base-content/70 mb-2 block text-sm font-medium'
              >
                {mode === 'flow' ? 'Target' : 'Maximum'} Flow{' '}
                {mode === 'pressure' && '(0 = Ignore)'}
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
                    min={mode === 'flow' ? 0.1 : 0}
                  />
                  <span aria-label='grams per second'>g/s</span>
                </label>
              </div>
            </div>
          )}
        </div>
      )}

      <div className='grid grid-cols-1 gap-4'>
        <div className='form-control'>
          <SegmentedControl
            label='Ramp Style'
            ariaLabel='Ramp style selection'
            value={phase.transition?.type || 'instant'}
            onChange={next => onFieldChange('transition', { ...phase.transition, type: next })}
            options={[
              { value: 'instant', label: 'Instant' },
              { value: 'linear', label: 'Linear' },
              { value: 'ease-in', label: 'Ease In' },
              { value: 'ease-out', label: 'Ease Out' },
              { value: 'ease-in-out', label: 'Ease In Out' },
            ]}
          />
        </div>
      </div>
      {phase.transition?.type !== 'instant' && (
        <>
          <div className='grid grid-cols-1 gap-4'>
            <div className='form-control'>
              <SegmentedControl
                label='Ramp Target'
                ariaLabel='Ramp target selection'
                value={phase.transition?.target || 'time'}
                onChange={next =>
                  onFieldChange('transition', { ...phase.transition, target: next })
                }
                options={[
                  { value: 'time', label: 'Time' },
                  { value: 'volumetric', label: 'Weight in phase' },
                  { value: 'pumped', label: 'Water pumped in phase', ariaLabel: 'Water pumped' },
                ]}
              />
            </div>
          </div>
          <div className='grid grid-cols-1 gap-4'>
            <div className='form-control'>
              <label
                htmlFor={`phase-${index}-transition-duration`}
                className='mb-2 block text-sm font-medium'
              >
                Ramp Length
              </label>
              <div className='input-group'>
                <label htmlFor={`phase-${index}-transition-duration`} className='input w-full'>
                  <NumberInput
                    id={`phase-${index}-transition-duration`}
                    className='grow'
                    value={phase.transition?.duration || 0}
                    onCommit={value =>
                      onFieldChange('transition', {
                        ...phase.transition,
                        duration: value,
                      })
                    }
                    aria-label={`Transition duration in ${rampUnit}`}
                    min={0}
                    step='0.1'
                  />
                  <span aria-label={rampUnit}>{rampUnit}</span>
                </label>
              </div>
            </div>
            <div className='form-control'>
              <SegmentedControl
                label='Start Ramp from'
                ariaLabel='Start ramp from'
                value={phase.transition?.adaptive ? 'current' : 'previous'}
                onChange={next =>
                  onFieldChange('transition', { ...phase.transition, adaptive: next === 'current' })
                }
                options={[
                  {
                    value: 'previous',
                    label: 'Previous target',
                    ariaLabel: 'Start from previous setpoint',
                  },
                  { value: 'current', label: 'Current value' },
                ]}
              />
            </div>
          </div>
        </>
      )}

      <div className='mt-4 flex flex-row items-center gap-2'>
        <h3 className='text-base-content/70 text-sm font-medium'>Stop when</h3>
        <div className='dropdown'>
          <Tooltip content='Add stop condition'>
            <div
              tabIndex='0'
              role='button'
              className='text-base-content/60 hover:text-base-content hover:bg-base-content/10 cursor-pointer rounded-lg px-2 py-1 transition-colors'
              aria-label='Add target'
            >
              <FontAwesomeIcon icon={faPlus} />
            </div>
          </Tooltip>
          <ul
            tabIndex='0'
            className='menu dropdown-content bg-base-100 rounded-box z-1 w-52 p-2 shadow-sm'
          >
            {availableTargetTypes.map(t => (
              <li key={`${t.type}-${t.operator}`}>
                <span
                  role='button'
                  onClick={e =>
                    onTargetAdd({
                      type: t.type,
                      operator: t.operator,
                      value: 0,
                    })
                  }
                >
                  {t.label}
                </span>
              </li>
            ))}
            {availableTargetTypes.length === 0 && (
              <li className='italic'>
                <a>No more types available</a>
              </li>
            )}
          </ul>
        </div>
      </div>
      {targets.map((target, idx) => (
        <>
          {idx !== 0 && (
            <div key={`sep-${idx}`} className='divider'>
              OR
            </div>
          )}
          <ExtendedPhaseTarget
            onChange={value => onTargetChange(idx, value)}
            onRemove={() => onTargetRemove(idx)}
            key={`target-${idx}`}
            target={target}
            index={idx}
          />
        </>
      ))}
    </div>
  );
}
