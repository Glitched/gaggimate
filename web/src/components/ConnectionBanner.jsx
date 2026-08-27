import { computed } from '@preact/signals';
import { useEffect, useState } from 'preact/hooks';
import { machine } from '../services/ApiService.js';

const connected = computed(() => machine.value.connected);

// Slim banner shown while the WebSocket to the machine is down. Without it a
// disconnect just freezes the numbers on screen with no visible signal.
export function ConnectionBanner() {
  const isConnected = connected.value;
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    if (isConnected) {
      setVisible(false);
      return;
    }
    // Grace period so the banner doesn't flash during the initial page-load
    // connect or a sub-second blip.
    const timeoutId = setTimeout(() => setVisible(true), 2000);
    return () => clearTimeout(timeoutId);
  }, [isConnected]);

  if (!visible) return null;

  return (
    <div
      role='status'
      aria-live='polite'
      className='bg-warning text-warning-content flex items-center justify-center gap-2 px-4 py-1.5 text-sm font-medium'
    >
      <span className='loading loading-spinner loading-xs' aria-hidden='true' />
      <span>Machine disconnected — reconnecting…</span>
    </div>
  );
}
