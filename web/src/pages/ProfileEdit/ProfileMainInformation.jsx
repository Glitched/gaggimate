import Card from '../../components/Card.jsx';
import { NumberField } from '../../components/NumberField.jsx';

export function ProfileMainInformation(props) {
  return (
    <Card sm={10} title='Profile Information'>
      <div className='form-control'>
        <label htmlFor='label' className='text-base-content/70 mb-2 block text-sm font-medium'>
          Title
        </label>
        <input
          id='label'
          name='label'
          className='input input-bordered w-full'
          value={props.data?.label}
          onChange={props.onChangeLabel}
          aria-label='Enter a name for this profile'
          required
        />
      </div>
      <div className='form-control'>
        <label
          htmlFor='description'
          className='text-base-content/70 mb-2 block text-sm font-medium'
        >
          Description
        </label>
        <textarea
          id='description'
          name='description'
          rows={2}
          className='textarea textarea-bordered w-full text-wrap'
          value={props.data?.description}
          onChange={props.onChangeDescription}
          aria-label='Optional description for this profile'
        />
      </div>
      {/* Both are short, so they share a row rather than taking one each; the
          temperature field is sized to a three-digit value instead of spanning
          the card. */}
      <div className='flex flex-wrap items-end gap-6'>
        <NumberField
          id='temperature'
          label='Temperature'
          unit='°C'
          unitLabel='degrees Celsius'
          value={props.data?.temperature}
          onCommit={props.onCommitTemperature}
          ariaLabel='Temperature in degrees Celsius'
          min={0}
          max={150}
          step='0.1'
        />
        <div className='form-control'>
          <label
            htmlFor='utility'
            className='text-base-content/70 mb-2 block text-sm font-medium'
            aria-label='Utility profile toggle'
          >
            Utility profile
          </label>
          {/* h-12 matches the input beside it so the two controls sit on the
              same baseline rather than the toggle floating high. */}
          <div className='flex h-12 items-center'>
            <input
              id='utility'
              name='utility'
              type='checkbox'
              className='toggle toggle-primary'
              checked={!!props.data?.utility}
              onChange={props.onChangeUtility}
            />
          </div>
        </div>
      </div>
    </Card>
  );
}
