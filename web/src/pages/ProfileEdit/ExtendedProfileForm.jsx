import Card from '../../components/Card.jsx';
import { Spinner } from '../../components/Spinner.jsx';
import { ExtendedProfileChart } from '../../components/ExtendedProfileChart.jsx';
import { useMemo, useState } from 'preact/hooks';
import { ExtendedPhase } from './ExtendedPhase.jsx';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faChevronLeft } from '@fortawesome/free-solid-svg-icons/faChevronLeft';
import { faChevronRight } from '@fortawesome/free-solid-svg-icons/faChevronRight';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faArrowLeft } from '@fortawesome/free-solid-svg-icons/faArrowLeft';
import { faArrowRight } from '@fortawesome/free-solid-svg-icons/faArrowRight';
import { ProfileMainInformation } from './ProfileMainInformation.jsx';
import { getProfilePhases, movePhase, removePhaseAt, updatePhaseAt } from './profilePhases.js';
import { useConfirmAction } from '../../hooks/useConfirmAction.js';

export function ExtendedProfileForm(props) {
  const { data, onChange, onSave, saving = true, pressureAvailable = false } = props;
  const phases = getProfilePhases(data);
  const [currentPhaseIndex, setCurrentPhaseIndex] = useState(0);
  const { armed: removeArmed, armOrRun: confirmOrRemove } = useConfirmAction(4000);

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
          transition: {
            type: 'instant',
            duration: 0,
            adaptive: true,
            target: 'time',
          },
          targets: [],
        },
      ],
    });
    setCurrentPhaseIndex(phases.length);
  };

  const onPhaseRemove = index => {
    onChange({
      ...data,
      phases: removePhaseAt(phases, index),
    });
    // Stay near the removed phase instead of jumping back to the first one.
    setCurrentPhaseIndex(Math.max(0, Math.min(index, phases.length - 2)));
  };

  const onPhaseMove = direction => {
    const target = currentPhaseIndex + direction;
    if (target < 0 || target >= phases.length) return;
    onChange({
      ...data,
      phases: movePhase(phases, currentPhaseIndex, target),
    });
    // Follow the phase to its new position.
    setCurrentPhaseIndex(target);
  };

  const currentPhase = phases[currentPhaseIndex];

  // The chart only reads `phases`; a stable object identity keyed on them
  // keeps title/description keystrokes from re-rendering the chart.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  const chartData = useMemo(() => ({ ...data, phases }), [phases]);

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
          onChangeTemperature={e => onFieldChange('temperature', e.target.value)}
          onChangeUtility={e => onFieldChange('utility', !!e.target.checked)}
        />
        <Card sm={10}>
          <ExtendedProfileChart
            data={chartData}
            selectedPhase={currentPhaseIndex}
            className='max-h-72 w-full'
            onPhaseClick={setCurrentPhaseIndex}
          />
        </Card>
        <Card sm={10}>
          <div className='card-header flex items-center gap-4'>
            <h2 className='card-title flex-grow text-lg sm:text-xl'>Phases</h2>
            <h5 className='card-subtitle text-sm sm:text-base'>
              {phases.length > 0 ? `${currentPhaseIndex + 1} / ${phases.length}` : '0 / 0'}
            </h5>
            <div>
              <div className='join' role='group' aria-label='Phase navigation'>
                <button
                  type='button'
                  className={`join-item btn btn-outline max-sm:btn-sm`}
                  aria-label='Previous'
                  disabled={currentPhaseIndex === 0}
                  onClick={() => setCurrentPhaseIndex(currentPhaseIndex - 1)}
                >
                  <FontAwesomeIcon icon={faChevronLeft} />
                </button>
                <button
                  type='button'
                  className={`join-item btn btn-outline max-sm:btn-sm`}
                  aria-label='Next'
                  disabled={phases.length === 0 || currentPhaseIndex === phases.length - 1}
                  onClick={() => setCurrentPhaseIndex(currentPhaseIndex + 1)}
                >
                  <FontAwesomeIcon icon={faChevronRight} />
                </button>
              </div>
            </div>
            <div className='join' role='group' aria-label='Reorder phase'>
              <button
                type='button'
                className={`join-item btn btn-outline max-sm:btn-sm`}
                aria-label='Move phase earlier'
                title='Move phase earlier'
                disabled={phases.length === 0 || currentPhaseIndex === 0}
                onClick={() => onPhaseMove(-1)}
              >
                <FontAwesomeIcon icon={faArrowLeft} />
              </button>
              <button
                type='button'
                className={`join-item btn btn-outline max-sm:btn-sm`}
                aria-label='Move phase later'
                title='Move phase later'
                disabled={phases.length === 0 || currentPhaseIndex === phases.length - 1}
                onClick={() => onPhaseMove(1)}
              >
                <FontAwesomeIcon icon={faArrowRight} />
              </button>
            </div>
            <button
              type='button'
              className={`join-item btn btn-outline max-sm:btn-sm`}
              aria-label='Add phase'
              onClick={() => onPhaseAdd()}
            >
              <FontAwesomeIcon icon={faPlus} />
            </button>
            <button
              type='button'
              className={`join-item btn max-sm:btn-sm ${removeArmed ? 'btn-error' : 'btn-outline text-error'}`}
              aria-label={removeArmed ? 'Click again to remove phase' : 'Remove phase'}
              title={removeArmed ? 'Click again to confirm' : 'Remove phase'}
              disabled={phases.length === 0}
              onClick={() => confirmOrRemove(() => onPhaseRemove(currentPhaseIndex))}
            >
              <FontAwesomeIcon icon={faTrashCan} />
            </button>
          </div>
          <div className='space-y-4' role='group' aria-label='Brew phases configuration'>
            {currentPhase ? (
              <ExtendedPhase
                phase={currentPhase}
                index={currentPhaseIndex}
                onChange={phase => onPhaseChange(currentPhaseIndex, phase)}
                onRemove={() => onPhaseRemove(currentPhaseIndex)}
                pressureAvailable={pressureAvailable}
              />
            ) : (
              <p className='text-base-content/60 text-sm'>
                No phases yet. Add a phase to configure brewing.
              </p>
            )}
          </div>
        </Card>
      </div>

      <div className='pt-4 lg:col-span-10'>
        <div className='flex flex-col gap-2 sm:flex-row'>
          <a href='/profiles' className='btn btn-outline'>
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
