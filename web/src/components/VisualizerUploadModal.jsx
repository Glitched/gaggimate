import { useState, useEffect } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { faEyeSlash } from '@fortawesome/free-solid-svg-icons/faEyeSlash';
import { faSpinner } from '@fortawesome/free-solid-svg-icons/faSpinner';

export default function VisualizerUploadModal({
  isOpen,
  onClose,
  onUpload,
  isUploading = false,
  shotInfo,
}) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [rememberCredentials, setRememberCredentials] = useState(false);

  const handleSubmit = async e => {
    e.preventDefault();
    if (!username.trim() || !password.trim()) {
      alert('Please enter both username and password');
      return;
    }

    try {
      await onUpload(username.trim(), password, rememberCredentials);

      // Remember the username only — never persist the password: localStorage
      // is plaintext and shared with anything on this origin.
      if (rememberCredentials) {
        localStorage.setItem('visualizer_username', username.trim());
        localStorage.setItem('visualizer_remember', 'true');
      } else {
        localStorage.removeItem('visualizer_username');
        localStorage.removeItem('visualizer_remember');
      }
      // Clean up passwords persisted by older versions.
      localStorage.removeItem('visualizer_password');

      // Clear form and close modal on success
      setUsername('');
      setPassword('');
      onClose();
    } catch (error) {
      // Error handling is done in parent component
      console.error('Upload failed:', error);
    }
  };

  const handleClose = () => {
    if (!isUploading) {
      setUsername('');
      setPassword('');
      onClose();
    }
  };

  // Load the saved username when the modal opens (passwords are never stored).
  useEffect(() => {
    if (isOpen) {
      const savedUsername = localStorage.getItem('visualizer_username');
      const savedRemember = localStorage.getItem('visualizer_remember') === 'true';

      if (savedRemember && savedUsername) {
        setUsername(savedUsername);
        setRememberCredentials(true);
      }
    }
  }, [isOpen]);

  if (!isOpen) return null;

  return (
    <div className='fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4'>
      <div className='bg-base-100 max-h-[90vh] w-full max-w-md overflow-y-auto rounded-lg shadow-xl'>
        <div className='p-6'>
          <div className='mb-4 flex items-center justify-between'>
            <h3 className='text-lg font-semibold'>Upload to Visualizer.coffee</h3>
            {!isUploading && (
              <button
                onClick={handleClose}
                className='btn btn-ghost btn-sm btn-circle'
                aria-label='Close'
              >
                ✕
              </button>
            )}
          </div>

          {shotInfo && (
            <div className='bg-base-200 mb-4 rounded-md p-3'>
              <p className='text-base-content/70 text-sm'>
                <strong>Shot:</strong> {shotInfo.profile}
              </p>
              <p className='text-base-content/70 text-sm'>
                <strong>Date:</strong> {new Date(shotInfo.timestamp * 1000).toLocaleString()}
              </p>
              <p className='text-base-content/70 text-sm'>
                <strong>Duration:</strong> {(shotInfo.duration / 1000).toFixed(1)}s
              </p>
              {shotInfo.volume > 0 && (
                <p className='text-base-content/70 text-sm'>
                  <strong>Yield:</strong> {shotInfo.volume}g
                </p>
              )}
            </div>
          )}

          <form onSubmit={handleSubmit} className='space-y-4' name='visualizer-login' method='post'>
            <div>
              <label htmlFor='username' className='mb-1 block text-sm font-medium'>
                Visualizer.coffee Username
              </label>
              <input
                id='username'
                name='username'
                type='text'
                value={username}
                onChange={e => setUsername(e.target.value)}
                disabled={isUploading}
                className='input input-bordered w-full'
                placeholder='Enter your username'
                autoComplete='username'
                required
              />
            </div>

            <div>
              <label htmlFor='password' className='mb-1 block text-sm font-medium'>
                Password
              </label>
              <div className='relative'>
                <input
                  id='password'
                  name='password'
                  type={showPassword ? 'text' : 'password'}
                  value={password}
                  onChange={e => setPassword(e.target.value)}
                  disabled={isUploading}
                  className='input input-bordered w-full pr-10'
                  placeholder='Enter your password'
                  autoComplete='current-password'
                  required
                />
                <button
                  type='button'
                  onClick={() => setShowPassword(!showPassword)}
                  disabled={isUploading}
                  aria-label={showPassword ? 'Hide password' : 'Show password'}
                  className='text-base-content/50 hover:text-base-content/80 absolute inset-y-0 right-0 z-10 flex items-center pr-3 disabled:opacity-50'
                >
                  <FontAwesomeIcon icon={showPassword ? faEyeSlash : faEye} />
                </button>
              </div>
            </div>

            <div className='flex items-center'>
              <input
                id='remember'
                type='checkbox'
                checked={rememberCredentials}
                onChange={e => setRememberCredentials(e.target.checked)}
                disabled={isUploading}
                className='checkbox checkbox-sm'
              />
              <label htmlFor='remember' className='text-base-content/70 ml-2 text-sm'>
                Remember username
              </label>
            </div>

            <div className='flex justify-end space-x-3 pt-4'>
              <button
                type='button'
                onClick={handleClose}
                disabled={isUploading}
                className='btn btn-ghost btn-sm'
              >
                Cancel
              </button>
              <button
                type='submit'
                disabled={isUploading || !username.trim() || !password.trim()}
                className='btn btn-primary btn-sm gap-2'
              >
                {isUploading && <FontAwesomeIcon icon={faSpinner} spin />}
                <span>{isUploading ? 'Uploading...' : 'Upload Shot'}</span>
              </button>
            </div>
          </form>

          <div className='text-base-content/60 mt-4 text-xs'>
            <p>
              Your credentials are only used for this upload. Only your username is stored locally
              if you choose to remember it — your password is never saved.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
