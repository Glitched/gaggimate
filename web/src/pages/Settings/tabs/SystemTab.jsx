import { useCallback, useContext, useEffect, useState } from 'preact/hooks';
import { computed } from '@preact/signals';
import { ApiServiceContext, machine } from '../../../services/ApiService.js';
import { downloadJson } from '../../../utils/download.js';
import { showToast } from '../../../services/toast.js';
import { SystemTabSkeleton } from '../../../components/skeletons/SettingsSkeletons.jsx';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCheck } from '@fortawesome/free-solid-svg-icons/faCheck';
import { Spinner } from '../../../components/Spinner.jsx';
import Section from '../../../components/Card.jsx';

const imageUrlToBase64 = async blob => {
  return new Promise((onSuccess, onError) => {
    try {
      const reader = new FileReader();
      reader.onload = function () {
        onSuccess(this.result);
      };
      reader.onerror = function () {
        onError(new Error('FileReader failed to read image blob.'));
      };
      reader.readAsDataURL(blob);
    } catch (e) {
      onError(e instanceof Error ? e : new Error(String(e)));
    }
  });
};

const getRssiStatusClass = rssi => {
  if (rssi < -90) return 'status-error';
  if (rssi < -80) return 'status-warning';
  return 'status-success';
};

// Module-level computed so the connection flag only re-renders subscribers
// when the boolean flips, not on every 500ms status frame.
const connected = computed(() => machine.value.connected);

// Reads the live signal inside its own component so the 2 Hz status push only
// re-renders this one line, not the whole System tab.
function ControllerSignalStrength() {
  const rssi = machine.value.status.rssi;
  const lat = machine.value.status.lat;
  return (
    <span className='text-base-content flex items-center gap-2 font-semibold'>
      {rssi}dB (Roundtrip: {lat} ms)
      <span className={`indicator-item status ${getRssiStatusClass(rssi)}`} />
    </span>
  );
}

function OtaProgressView({ phase, progress }) {
  const getOtaPhaseText = p => {
    switch (p) {
      case 1:
        return 'Updating Display firmware';
      case 2:
        return 'Updating Display filesystem';
      case 3:
        return 'Updating controller firmware';
      default:
        return 'Finished';
    }
  };

  return (
    <div className='flex flex-col items-center gap-4 py-12'>
      <Spinner size={8} />
      <span className='text-base-content text-xl font-medium'>{getOtaPhaseText(phase)}</span>
      <span className='text-base-content text-lg font-medium'>{phase === 4 ? 100 : progress}%</span>
      {phase === 4 && (
        <a href='/' className='btn btn-primary'>
          Back
        </a>
      )}
    </div>
  );
}

function StorageAndMemorySection({ formData }) {
  return (
    <Section title='Storage & Memory' className='h-full'>
      <div className='grid grid-cols-1 gap-6'>
        {formData.spiffsTotal !== undefined && (
          <div className='flex flex-col space-y-2'>
            <span className='text-base-content/70 text-sm font-medium'>Storage (LittleFS)</span>
            <div className='bg-base-300 h-3 w-full overflow-hidden rounded'>
              <div
                className='bg-primary h-full transition-all'
                style={{ width: `${formData.spiffsUsedPct || 0}%` }}
              />
            </div>
            <div className='text-base-content/60 text-xs'>
              {((formData.spiffsUsed || 0) / 1024).toFixed(1)} KB /{' '}
              {(formData.spiffsTotal / 1024).toFixed(1)} KB ({formData.spiffsUsedPct}%)
            </div>
          </div>
        )}

        {formData.sdTotal !== undefined && (
          <div className='flex flex-col space-y-2'>
            <span className='text-base-content/70 text-sm font-medium'>Storage (SD-Card)</span>
            <div className='bg-base-300 h-3 w-full overflow-hidden rounded'>
              <div
                className='bg-primary h-full transition-all'
                style={{ width: `${formData.sdUsedPct || 0}%` }}
              />
            </div>
            <div className='text-base-content/60 text-xs'>
              {((formData.sdUsed || 0) / 1024 / 1024).toFixed(1)} MB /{' '}
              {(formData.sdTotal / 1024 / 1024).toFixed(1)} MB ({formData.sdUsedPct}%)
            </div>
          </div>
        )}

        {formData.heapTotal !== undefined && (
          <div className='flex flex-col space-y-2'>
            <span className='text-base-content/70 text-sm font-medium'>Free Memory (Heap)</span>
            <div className='bg-base-300 h-3 w-full overflow-hidden rounded'>
              <div
                className='bg-primary h-full transition-all'
                style={{
                  width: `${formData.heapTotal > 0 ? ((formData.heapTotal - formData.heapFree) / formData.heapTotal) * 100 : 0}%`,
                }}
              />
            </div>
            <div className='text-base-content/60 text-xs'>
              {((formData.heapTotal - formData.heapFree || 0) / 1024).toFixed(1)} kB /{' '}
              {(formData.heapTotal / 1024).toFixed(1)} kB (
              {(formData.heapTotal > 0
                ? ((formData.heapTotal - formData.heapFree) / formData.heapTotal) * 100
                : 0
              ).toFixed(2)}
              %) (Frag:{' '}
              {(formData.heapFree > 0
                ? 100 - (formData.heapLargest * 100) / formData.heapFree
                : 0
              ).toFixed(2)}
              %)
            </div>
          </div>
        )}
      </div>
    </Section>
  );
}

function MaintenanceSection({
  downloadSupportData,
  onHistoryRebuild,
  rebuilding,
  rebuilt,
  rebuildProgress,
}) {
  return (
    <Section title='Maintenance & Support' className='h-full'>
      <div className='flex flex-col flex-wrap gap-4 sm:flex-row'>
        <button type='button' className='btn btn-outline btn-sm' onClick={downloadSupportData}>
          Download Support Data
        </button>

        <button
          type='button'
          className='btn btn-outline btn-sm'
          onClick={onHistoryRebuild}
          disabled={rebuilding}
        >
          Rebuild Shot History
          {rebuilding && (
            <>
              <Spinner size={4} className='ml-2' />
              {rebuildProgress.total > 0 && (
                <span className='ml-2 text-xs'>
                  {rebuildProgress.current}/{rebuildProgress.total}
                </span>
              )}
            </>
          )}
          {rebuilt && (
            <span className='text-success ml-2'>
              <FontAwesomeIcon icon={faCheck}></FontAwesomeIcon>
            </span>
          )}
        </button>
      </div>

      {rebuilding && (
        <div className='mt-4 max-w-md'>
          <div className='text-base-content/70 mb-1 text-xs'>
            {rebuildProgress.status === 'starting' ||
            rebuildProgress.status === 'scanning' ||
            rebuildProgress.total === 0
              ? 'Scanning shot history files...'
              : `Processing shot history files (${rebuildProgress.current}/${rebuildProgress.total})`}
          </div>
          <div className='bg-base-300 h-2 w-full overflow-hidden rounded'>
            <div
              className={`h-full transition-all duration-300 ${
                rebuildProgress.total === 0 ? 'bg-primary animate-pulse' : 'bg-primary'
              }`}
              style={{
                width:
                  rebuildProgress.total > 0
                    ? `${(rebuildProgress.current / rebuildProgress.total) * 100}%`
                    : '30%',
              }}
            />
          </div>
        </div>
      )}
    </Section>
  );
}

export function SystemTab() {
  const apiService = useContext(ApiServiceContext);
  const [isLoading, setIsLoading] = useState(true);
  const [submitting, setSubmitting] = useState(false);
  const [formData, setFormData] = useState({});
  const [phase, setPhase] = useState(0);
  const [progress, setProgress] = useState(0);
  const [channel, setChannel] = useState(null);

  const downloadSupportData = useCallback(async () => {
    try {
      const settingsResponse = await fetch(`/api/settings`);
      const data = await settingsResponse.json();
      delete data.wifiPassword;
      delete data.haPassword;
      const coredumpBlob = await fetch(`/api/core-dump`).then(r => r.blob());
      let coredump = await imageUrlToBase64(coredumpBlob);
      coredump = coredump.substring(coredump.indexOf('base64,') + 7);
      const supportFile = {
        settings: data,
        versions: formData,
        coredump,
      };
      const ts = Date.now();
      downloadJson(supportFile, `support-${ts}.dat`);
    } catch (e) {
      console.error('Error downloading support data:', e);
      showToast('Downloading support data failed — check the machine connection and try again.', {
        type: 'error',
      });
    }
  }, [formData]);

  useEffect(() => {
    const listenerId = apiService.on('res:ota-settings', msg => {
      setFormData(msg);
      setChannel(current => current ?? msg.channel);
      setIsLoading(false);
      setSubmitting(false);
    });
    return () => {
      apiService.off('res:ota-settings', listenerId);
    };
  }, [apiService]);

  useEffect(() => {
    const listenerId = apiService.on('evt:ota-progress', msg => {
      setProgress(msg.progress);
      setPhase(msg.phase);
    });
    return () => {
      apiService.off('evt:ota-progress', listenerId);
    };
  }, [apiService]);

  useEffect(() => {
    const listenerId = apiService.on('evt:history-rebuild-progress', msg => {
      setRebuildProgress({
        total: msg.total || 0,
        current: msg.current || 0,
        status: msg.status || '',
      });

      if (msg.status === 'completed' || msg.status === 'error') {
        setRebuilding(false);
        setRebuilt(msg.status === 'completed');
      }
    });
    return () => {
      apiService.off('evt:history-rebuild-progress', listenerId);
    };
  }, [apiService]);

  // Request OTA settings as soon as the socket is up, and again on every
  // reconnect while still loading — the previous fixed 500 ms timer threw when
  // the socket wasn't open yet and left the tab wedged on the skeleton.
  useEffect(() => {
    if (!connected.value || !isLoading) return;
    try {
      apiService.send({ tp: 'req:ota-settings' });
    } catch (error) {
      console.error('Failed to request OTA settings:', error);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- connected.value is a signal read; the render-time read subscribes this component
  }, [connected.value, isLoading, apiService]);

  const onSubmit = useCallback(
    async e => {
      e.preventDefault();
      try {
        apiService.send({ tp: 'req:ota-settings', update: true, channel });
        setSubmitting(true);
      } catch (error) {
        console.error('Failed to save update channel:', error);
        showToast('Machine not connected — could not save the update channel.', { type: 'error' });
      }
    },
    [apiService, channel],
  );

  // A dropped response must not disable the update buttons until remount.
  useEffect(() => {
    if (!submitting) return;
    const timeoutId = setTimeout(() => {
      setSubmitting(false);
      showToast('The machine did not respond — try saving the channel again.', { type: 'error' });
    }, 15000);
    return () => clearTimeout(timeoutId);
  }, [submitting]);

  const onUpdate = useCallback(
    component => {
      try {
        apiService.send({ tp: 'req:ota-start', cp: component });
      } catch (error) {
        console.error('Failed to start update:', error);
        showToast('Machine not connected — could not start the update.', { type: 'error' });
      }
    },
    [apiService],
  );

  // Firmware flashes are disruptive — require a second click to confirm.
  const [pendingUpdate, setPendingUpdate] = useState(null);

  useEffect(() => {
    if (!pendingUpdate) return;
    const timeoutId = setTimeout(() => setPendingUpdate(null), 5000);
    return () => clearTimeout(timeoutId);
  }, [pendingUpdate]);

  const requestUpdate = useCallback(
    component => {
      if (pendingUpdate === component) {
        setPendingUpdate(null);
        onUpdate(component);
      } else {
        setPendingUpdate(component);
      }
    },
    [pendingUpdate, onUpdate],
  );

  const [rebuilding, setRebuilding] = useState(false);
  const [rebuilt, setRebuilt] = useState(false);
  const [rebuildProgress, setRebuildProgress] = useState({ total: 0, current: 0, status: '' });

  const onHistoryRebuild = useCallback(async () => {
    setRebuilt(false);
    setRebuilding(true);
    setRebuildProgress({ total: 0, current: 0, status: 'starting' });
    apiService.send({ tp: 'req:history:rebuild' });
  }, [apiService]);

  if (phase > 0) {
    return <OtaProgressView phase={phase} progress={progress} />;
  }

  if (isLoading) {
    return <SystemTabSkeleton />;
  }

  return (
    <div className='space-y-4 sm:space-y-6 lg:grid lg:grid-cols-2 lg:gap-4'>
      {/* Firmware updates channel */}
      <Section title='System Version & Updates' className='h-full'>
        <form onSubmit={onSubmit} className='space-y-4'>
          <div className='form-control max-w-md'>
            <label htmlFor='channel' className='mb-2 block text-sm font-medium'>
              Update Channel
            </label>
            <div className='flex w-full items-center gap-2'>
              <select
                id='channel'
                name='channel'
                className='select select-bordered grow'
                value={channel ?? formData.channel}
                onChange={e => setChannel(e.currentTarget.value)}
              >
                <option value='latest'>Stable</option>
                <option value='nightly'>Nightly</option>
              </select>
              <button type='submit' className='btn btn-secondary' disabled={submitting}>
                Save Channel & Refresh
              </button>
            </div>
          </div>
        </form>

        <div className='border-base-content/5 mt-6 grid grid-cols-1 gap-6 border-t pt-6 md:grid-cols-2'>
          <div className='flex flex-col space-y-1'>
            <span className='text-base-content/70 text-sm font-medium'>Hardware</span>
            <span className='text-base-content font-semibold'>{formData.hardware}</span>
          </div>

          <div className='flex flex-col space-y-1'>
            <span className='text-base-content/70 text-sm font-medium'>
              Controller Signal Strength
            </span>
            <ControllerSignalStrength />
          </div>

          <div className='flex flex-col space-y-2'>
            <span className='text-base-content/70 text-sm font-medium'>Controller Version</span>
            <div className='flex flex-wrap items-center gap-4'>
              <span className='text-base-content font-semibold break-all'>
                {formData.controllerVersion}
              </span>
              {formData.controllerUpdateAvailable && (
                <span className='text-primary text-sm font-bold'>
                  Update available: {formData.latestVersion}
                </span>
              )}
              <button
                type='button'
                className={`btn btn-sm ${pendingUpdate === 'controller' ? 'btn-warning' : 'btn-secondary'}`}
                disabled={!formData.controllerUpdateAvailable || submitting}
                onClick={() => requestUpdate('controller')}
              >
                {pendingUpdate === 'controller' ? 'Confirm update?' : 'Update Controller'}
              </button>
            </div>
          </div>

          <div className='flex flex-col space-y-2'>
            <span className='text-base-content/70 text-sm font-medium'>Display Version</span>
            <div className='flex flex-wrap items-center gap-4'>
              <span className='text-base-content font-semibold break-all'>
                {formData.displayVersion}
              </span>
              {formData.displayUpdateAvailable && (
                <span className='text-primary text-sm font-bold'>
                  Update available: {formData.latestVersion}
                </span>
              )}
              <button
                type='button'
                className={`btn btn-sm ${pendingUpdate === 'display' ? 'btn-warning' : 'btn-secondary'}`}
                disabled={!formData.displayUpdateAvailable || submitting}
                onClick={() => requestUpdate('display')}
              >
                {pendingUpdate === 'display' ? 'Confirm update?' : 'Update Display'}
              </button>
            </div>
          </div>
        </div>

        <div className='alert alert-warning mt-6'>
          <span>
            Make sure to backup your profiles from the profiles screen before updating the display.
          </span>
        </div>
      </Section>

      <StorageAndMemorySection formData={formData} />

      <MaintenanceSection
        downloadSupportData={downloadSupportData}
        onHistoryRebuild={onHistoryRebuild}
        rebuilding={rebuilding}
        rebuilt={rebuilt}
        rebuildProgress={rebuildProgress}
      />
    </div>
  );
}
