import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faEllipsisVertical } from '@fortawesome/free-solid-svg-icons/faEllipsisVertical';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { useCallback, useEffect, useRef, useState, useContext } from 'preact/hooks';
import { useRoute } from 'preact-iso';
import { computed } from '@preact/signals';
import {
  ApiServiceContext,
  machine,
  prefetchSettings,
  updateSettingsCache,
  getCachedSettings,
} from '../../services/ApiService.js';
import {
  DASHBOARD_LAYOUTS,
  setDashboardLayout,
  setClock24h,
} from '../../utils/dashboardManager.js';
import { downloadJson } from '../../utils/download.js';
import { getStoredTheme, handleThemeChange } from '../../utils/themeManager.js';
import { showToast } from '../../services/toast.js';
import { useUnsavedChangesGuard } from '../../hooks/useUnsavedChangesGuard.js';

import PageLayout from '../../components/PageLayout.jsx';
import PageHeader from '../../components/PageHeader.jsx';
import TabBar from '../../components/TabBar.jsx';

import lazy from 'preact-iso/lazy';

import { StickyFormFooter } from './StickyFormFooter.jsx';
import {
  GeneralTabSkeleton,
  MachineTabSkeleton,
  PluginsTabSkeleton,
  BluetoothTabSkeleton,
  SystemTabSkeleton,
} from '../../components/skeletons/SettingsSkeletons.jsx';
import { GeneralTab } from './tabs/GeneralTab.jsx';

const LazyMachineTab = lazy(() => import('./tabs/MachineTab.jsx').then(m => m.MachineTab));
const LazyCalibrationTab = lazy(() =>
  import('./tabs/CalibrationTab.jsx').then(m => m.CalibrationTab),
);
const LazyPluginsTab = lazy(() => import('./tabs/PluginsTab.jsx').then(m => m.PluginsTab));
const LazyBluetoothTab = lazy(() => import('./tabs/BluetoothTab.jsx').then(m => m.BluetoothTab));
const LazySystemTab = lazy(() => import('./tabs/SystemTab.jsx').then(m => m.SystemTab));

const loadMachineTab = () => import('./tabs/MachineTab.jsx');
const loadCalibrationTab = () => import('./tabs/CalibrationTab.jsx');
const loadPluginsTab = () => import('./tabs/PluginsTab.jsx');
const loadBluetoothTab = () => import('./tabs/BluetoothTab.jsx');
const loadSystemTab = () => import('./tabs/SystemTab.jsx');

// Icons
import { faSliders } from '@fortawesome/free-solid-svg-icons/faSliders';
import { faTemperatureHalf } from '@fortawesome/free-solid-svg-icons/faTemperatureHalf';
import { faCrosshairs } from '@fortawesome/free-solid-svg-icons/faCrosshairs';
import { faPuzzlePiece } from '@fortawesome/free-solid-svg-icons/faPuzzlePiece';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faRotate } from '@fortawesome/free-solid-svg-icons/faRotate';

// Module-level computed so reading connection state during render only
// re-renders this page when the boolean flips — the machine signal itself is
// replaced by every 500ms status frame.
const connected = computed(() => machine.value.connected);

function splitPidString(pidString) {
  if (!pidString) return { pid: pidString, kf: '0.000' };
  const parts = pidString.split(',');
  if (parts.length >= 4) {
    return { pid: parts.slice(0, 3).join(','), kf: parts[3] };
  }
  return { pid: pidString, kf: '0.000' };
}

function splitButtons(buttonBehavior) {
  if (!buttonBehavior) return {};
  const [button0, button1, button2] = buttonBehavior.split(',');
  return { button0, button1, button2 };
}

function parseAutoWakeupSchedules(autowakeupSchedules) {
  const defaultSchedule = [{ time: '07:00', days: [true, true, true, true, true, true, true] }];
  if (!autowakeupSchedules) {
    return defaultSchedule;
  }
  const schedules = [];
  if (typeof autowakeupSchedules === 'string' && autowakeupSchedules.trim()) {
    const scheduleStrings = autowakeupSchedules.split(';');
    for (const scheduleStr of scheduleStrings) {
      const [time, daysStr] = scheduleStr.split('|');
      if (time && daysStr && daysStr.length === 7) {
        const days = daysStr.split('').map(d => d === '1');
        schedules.push({ time, days });
      }
    }
  }
  return schedules.length > 0 ? schedules : defaultSchedule;
}

function transformFetchedSettings(fetchedSettings) {
  if (!fetchedSettings) return {};
  const buttonFields = fetchedSettings.buttonBehavior
    ? splitButtons(fetchedSettings.buttonBehavior)
    : {};
  const settingsWithToggle = {
    ...fetchedSettings,
    ...buttonFields,
    standbyDisplayEnabled:
      fetchedSettings.standbyDisplayEnabled !== undefined
        ? fetchedSettings.standbyDisplayEnabled
        : fetchedSettings.standbyBrightness > 0,
    dashboardLayout: fetchedSettings.dashboardLayout || DASHBOARD_LAYOUTS.ORDER_FIRST,
  };

  if (fetchedSettings.pid) {
    const split = splitPidString(fetchedSettings.pid);
    settingsWithToggle.pid = split.pid;
    settingsWithToggle.kf = split.kf;
  }
  return settingsWithToggle;
}

const SETTINGS_BOOLEAN_KEYS = [
  'homekit',
  'boilerFillActive',
  'smartGrindActive',
  'homeAssistant',
  'momentaryButtons',
  'delayAdjust',
  'clock24hFormat',
  'autowakeupEnabled',
  'smartGrindToggle',
];

// Form-model keys that are folded into combined firmware keys (buttonBehavior,
// pid) or exist only client-side — never sent as-is.
const SETTINGS_CLIENT_ONLY_KEYS = new Set(['button0', 'button1', 'button2', 'kf', 'standbyDisplayEnabled']);

// Maps the form model onto the firmware's settings keys, with real booleans.
function buildSettingsPayload(formData, autowakeupSchedules) {
  const payload = {};
  for (const [key, value] of Object.entries(formData)) {
    if (value === undefined || value === null) continue;
    if (SETTINGS_CLIENT_ONLY_KEYS.has(key)) continue;
    payload[key] = SETTINGS_BOOLEAN_KEYS.includes(key) ? !!value : value;
  }

  if (
    formData.button0 !== undefined &&
    formData.button1 !== undefined &&
    formData.button2 !== undefined
  ) {
    payload.buttonBehavior = `${formData.button0},${formData.button1},${formData.button2}`;
  }

  if (formData.pid && formData.kf !== undefined) {
    payload.pid = `${formData.pid},${formData.kf}`;
  }

  payload.autowakeupSchedules = (autowakeupSchedules || [])
    .map(schedule => `${schedule.time}|${schedule.days.map(d => (d ? '1' : '0')).join('')}`)
    .join(';');

  if (!formData.standbyDisplayEnabled) {
    payload.standbyBrightness = 0;
  }

  return payload;
}

// The firmware applies only the keys present in the body (partial-update
// semantics), so send just what changed since load/save: unrelated settings
// are never rewritten and a boolean can no longer be cleared by omission.
function diffSettingsPayload(current, baseline) {
  const changed = {};
  for (const [key, value] of Object.entries(current)) {
    const prev = baseline[key];
    const isSame =
      typeof value === 'boolean' || typeof prev === 'boolean'
        ? !!value === !!prev
        : prev !== undefined && String(value) === String(prev);
    if (!isSame) {
      changed[key] = value;
    }
  }
  return changed;
}

export function Settings() {
  const apiService = useContext(ApiServiceContext);
  const { params } = useRoute();
  const tab = params.tab || 'general';
  const isFormTab = ['general', 'machine', 'plugins'].includes(tab);

  const [profiles, setProfiles] = useState([]);
  const [submitting, setSubmitting] = useState(false);
  const [formData, setFormData] = useState({});
  const [currentTheme, setCurrentTheme] = useState('light');
  const [showWifiPassword, setShowWifiPassword] = useState(false);
  const [showApPassword, setShowApPassword] = useState(false);
  const [autowakeupSchedules, setAutoWakeupSchedules] = useState([
    { time: '07:00', days: [true, true, true, true, true, true, true] },
  ]);

  const [fetchedSettings, setFetchedSettings] = useState(() => getCachedSettings());
  const [isLoading, setIsLoading] = useState(!fetchedSettings);
  const [loadError, setLoadError] = useState(false);

  useEffect(() => {
    if (!fetchedSettings) {
      prefetchSettings()
        .then(data => {
          setFetchedSettings(data);
          setIsLoading(false);
        })
        .catch(err => {
          console.error('Failed to prefetch settings:', err);
          // Show an explicit error instead of an editable empty form — saving
          // a form bound to {} would post blank values to the machine.
          setLoadError(true);
          setIsLoading(false);
        });
    }
  }, [fetchedSettings]);

  const retryLoad = useCallback(() => {
    // prefetchSettings clears its cache on rejection, so calling it again
    // issues a fresh request.
    setLoadError(false);
    setIsLoading(true);
    prefetchSettings()
      .then(data => {
        setFetchedSettings(data);
        setIsLoading(false);
      })
      .catch(err => {
        console.error('Failed to prefetch settings:', err);
        setLoadError(true);
        setIsLoading(false);
      });
  }, []);

  useEffect(() => {
    const loadProfiles = async () => {
      if (connected.value) {
        try {
          const response = await apiService.request({ tp: 'req:profiles:list', minimal: true });
          setProfiles(response.profiles);
        } catch (error) {
          console.error('Failed to load profiles:', error);
        }
      }
    };
    loadProfiles();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- connected.value is a signal read; the render-time read subscribes this component
  }, [connected.value, apiService]);

  const formRef = useRef();
  const dropdownRef = useRef(null);
  const [dropdownOpen, setDropdownOpen] = useState(false);

  useEffect(() => {
    if (!dropdownOpen) return;

    const handleOutsideClick = event => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setDropdownOpen(false);
      }
    };

    document.addEventListener('click', handleOutsideClick);
    return () => document.removeEventListener('click', handleOutsideClick);
  }, [dropdownOpen]);

  // Snapshot of the last loaded/saved state, for unsaved-changes detection
  // and for diffing what to send on save.
  const baselineRef = useRef({ form: '{}', schedules: '[]' });

  useEffect(() => {
    if (fetchedSettings) {
      const settingsWithToggle = transformFetchedSettings(fetchedSettings);
      const parsedSchedules = parseAutoWakeupSchedules(fetchedSettings.autowakeupSchedules);
      setAutoWakeupSchedules(parsedSchedules);
      setClock24h(!!fetchedSettings.clock24hFormat);
      setFormData(settingsWithToggle);
      baselineRef.current = {
        form: JSON.stringify(settingsWithToggle),
        schedules: JSON.stringify(parsedSchedules),
      };
    } else {
      setFormData({});
      setAutoWakeupSchedules([{ time: '07:00', days: [true, true, true, true, true, true, true] }]);
    }
  }, [fetchedSettings]);

  const isDirty =
    !isLoading &&
    !!fetchedSettings &&
    (JSON.stringify(formData) !== baselineRef.current.form ||
      JSON.stringify(autowakeupSchedules) !== baselineRef.current.schedules);

  // Tab switches within /settings keep this form mounted, so let them through.
  useUnsavedChangesGuard(isDirty, { ignorePrefix: '/settings' });

  useEffect(() => {
    setCurrentTheme(getStoredTheme());
  }, []);

  const onChange = key => {
    return e => {
      const inputValue = e.currentTarget.value;
      const toggleKeys = [
        'homekit',
        'boilerFillActive',
        'smartGrindActive',
        'smartGrindToggle',
        'homeAssistant',
        'momentaryButtons',
        'delayAdjust',
        'clock24hFormat',
        'autowakeupEnabled',
      ];
      if (key === 'standbyDisplayEnabled') {
        setFormData(prev => {
          const value = !prev.standbyDisplayEnabled;
          const next = { ...prev, [key]: value };
          if (!value) {
            next.standbyBrightness = 0;
          }
          return next;
        });
        return;
      }
      if (key === 'clock24hFormat') {
        setClock24h(!formData[key]);
      }
      if (key === 'dashboardLayout') {
        setDashboardLayout(inputValue);
      }
      // Functional update: two changes dispatched in the same tick (e.g. a
      // toggle plus a dependent field) must not clobber each other.
      setFormData(prev => ({
        ...prev,
        [key]: toggleKeys.includes(key) ? !prev[key] : inputValue,
      }));
    };
  };

  const setField = useCallback((key, value) => {
    setFormData(prev => ({ ...prev, [key]: value }));
  }, []);

  const addAutoWakeupSchedule = () => {
    setAutoWakeupSchedules(prev => [
      ...prev,
      {
        time: '07:00',
        days: [true, true, true, true, true, true, true],
      },
    ]);
  };

  const removeAutoWakeupSchedule = index => {
    setAutoWakeupSchedules(prev => (prev.length > 1 ? prev.filter((_, i) => i !== index) : prev));
  };

  // Replace the schedule objects instead of mutating them in place — the old
  // entries are shared with previous state snapshots.
  const updateAutoWakeupTime = (index, value) => {
    setAutoWakeupSchedules(prev =>
      prev.map((schedule, i) => (i === index ? { ...schedule, time: value } : schedule)),
    );
  };

  const updateAutoWakeupDay = (scheduleIndex, dayIndex, enabled) => {
    setAutoWakeupSchedules(prev =>
      prev.map((schedule, i) =>
        i === scheduleIndex
          ? { ...schedule, days: schedule.days.map((d, j) => (j === dayIndex ? enabled : d)) }
          : schedule,
      ),
    );
  };

  const onSubmit = useCallback(
    async (e, restart = false) => {
      if (e) e.preventDefault();
      setSubmitting(true);
      const form = formRef.current;

      let baselineForm = {};
      let baselineSchedules = [];
      try {
        baselineForm = JSON.parse(baselineRef.current.form || '{}');
        baselineSchedules = JSON.parse(baselineRef.current.schedules || '[]');
      } catch {
        // Corrupt baseline: fall back to sending the full payload.
      }
      const payload = diffSettingsPayload(
        buildSettingsPayload(formData, autowakeupSchedules),
        buildSettingsPayload(baselineForm, baselineSchedules),
      );
      if (restart) {
        payload.restart = true;
      }

      try {
        const response = await fetch(form.action, {
          method: 'post',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload),
        });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();

        const splitPid = data.pid ? splitPidString(data.pid) : null;
        const buttonFields = data.buttonBehavior ? splitButtons(data.buttonBehavior) : {};

        const updatedData = {
          ...data,
          ...(splitPid !== null ? { pid: splitPid.pid, kf: splitPid.kf } : {}),
          ...buttonFields,
          standbyDisplayEnabled:
            data.standbyBrightness > 0 ? formData.standbyDisplayEnabled : false,
        };

        updateSettingsCache(data);
        setFormData(updatedData);
        baselineRef.current = {
          form: JSON.stringify(updatedData),
          schedules: JSON.stringify(autowakeupSchedules),
        };
        showToast(restart ? 'Settings saved — restarting machine' : 'Settings saved', {
          type: 'success',
        });
      } catch (error) {
        console.error('Failed to save settings:', error);
        showToast('Saving settings failed — check the machine connection and try again.', {
          type: 'error',
        });
      } finally {
        setSubmitting(false);
      }
    },
    [formData, autowakeupSchedules],
  );

  const onExport = useCallback(() => {
    // Never write credentials to an export file (mirrors the support-data
    // download, which strips them for the same reason).
    const exportData = { ...formData };
    delete exportData.wifiPassword;
    delete exportData.apPassword;
    delete exportData.haPassword;
    downloadJson(exportData, 'settings.json');
  }, [formData]);

  const onUpload = function (evt) {
    if (evt.target.files.length) {
      const file = evt.target.files[0];
      const reader = new FileReader();
      reader.onload = e => {
        let data;
        try {
          data = JSON.parse(e.target.result);
        } catch {
          showToast('Import failed: the selected file is not valid JSON.', { type: 'error' });
          return;
        }
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
          showToast('Import failed: the selected file is not a settings export.', {
            type: 'error',
          });
          return;
        }
        // Merge over current values so exports without credentials don't blank
        // out the passwords already loaded in the form.
        setFormData(prev => ({ ...prev, ...data }));
        showToast('Settings imported — review and press Save to apply.', { type: 'info' });
      };
      reader.readAsText(file);
      // Allow re-selecting the same file to trigger another change event.
      evt.target.value = '';
    }
  };

  const settingsTabs = [
    { id: 'general', label: 'General', icon: faSliders },
    { id: 'machine', label: 'Machine', icon: faTemperatureHalf, preload: loadMachineTab },
    { id: 'calibration', label: 'Calibration', icon: faCrosshairs, preload: loadCalibrationTab },
    { id: 'plugins', label: 'Plugins', icon: faPuzzlePiece, preload: loadPluginsTab },
    { id: 'bluetooth', label: 'Bluetooth', icon: faBluetoothB, preload: loadBluetoothTab },
    { id: 'system', label: 'System & Updates', icon: faRotate, preload: loadSystemTab },
  ];

  return (
    <PageLayout>
      <PageHeader
        title='Settings'
        noStack={true}
        tabs={<TabBar tabs={settingsTabs} activeTab={tab} basePath='/settings' />}
        actions={
          <div
            className={`action-dropdown relative ${dropdownOpen ? 'action-dropdown-open' : ''}`}
            ref={dropdownRef}
          >
            <button
              onClick={() => setDropdownOpen(open => !open)}
              className='btn btn-ghost btn-circle text-base-content/85 hover:bg-base-content/10'
              aria-label='More options'
              aria-expanded={dropdownOpen}
            >
              <FontAwesomeIcon icon={faEllipsisVertical} size='lg' />
            </button>
            <ul className='menu action-dropdown-menu bg-base-100 rounded-box border-base-content/10 right-0 z-50 mt-1 w-52 border p-2 shadow-lg'>
              <li>
                <button
                  type='button'
                  onClick={() => {
                    onExport();
                    setDropdownOpen(false);
                  }}
                  className='justify-start gap-2 font-medium'
                  aria-label='Export settings'
                >
                  <FontAwesomeIcon icon={faFileExport} />
                  <span>Export Settings</span>
                </button>
              </li>
              <li>
                <button
                  type='button'
                  onClick={() => {
                    document.getElementById('settingsImport')?.click();
                    setDropdownOpen(false);
                  }}
                  className='justify-start gap-2 font-medium'
                  aria-label='Import settings'
                >
                  <FontAwesomeIcon icon={faFileImport} />
                  <span>Import Settings</span>
                </button>
              </li>
            </ul>
            <input
              onChange={onUpload}
              className='hidden'
              id='settingsImport'
              type='file'
              accept='.json,application/json'
            />
          </div>
        }
      />

      {loadError && !fetchedSettings && (
        <div className='alert alert-error my-4'>
          <span>
            Could not load settings from the machine. Check that it is powered on and reachable,
            then try again.
          </span>
          <button type='button' className='btn btn-sm' onClick={retryLoad}>
            Retry
          </button>
        </div>
      )}

      <form
        id='settings-page-form'
        key='settings'
        ref={formRef}
        method='post'
        action='/api/settings'
        onSubmit={onSubmit}
        className={isFormTab && !(loadError && !fetchedSettings) ? '' : 'hidden'}
      >
        {tab === 'general' &&
          (isLoading ? (
            <GeneralTabSkeleton />
          ) : (
            <GeneralTab
              formData={formData}
              onChange={onChange}
              profiles={profiles}
              currentTheme={currentTheme}
              setCurrentTheme={setCurrentTheme}
              handleThemeChange={handleThemeChange}
              showWifiPassword={showWifiPassword}
              setShowWifiPassword={setShowWifiPassword}
              showApPassword={showApPassword}
              setShowApPassword={setShowApPassword}
            />
          ))}
        {tab === 'machine' &&
          (isLoading ? (
            <MachineTabSkeleton />
          ) : (
            <LazyMachineTab formData={formData} onChange={onChange} setField={setField} />
          ))}
        {tab === 'plugins' &&
          (isLoading ? (
            <PluginsTabSkeleton />
          ) : (
            <LazyPluginsTab
              formData={formData}
              onChange={onChange}
              autowakeupSchedules={autowakeupSchedules}
              addAutoWakeupSchedule={addAutoWakeupSchedule}
              removeAutoWakeupSchedule={removeAutoWakeupSchedule}
              updateAutoWakeupTime={updateAutoWakeupTime}
              updateAutoWakeupDay={updateAutoWakeupDay}
            />
          ))}

        {isFormTab && (
          <StickyFormFooter submitting={submitting} onRestart={e => onSubmit(e, true)} />
        )}
      </form>

      {tab === 'calibration' && <LazyCalibrationTab formData={formData} setField={setField} />}
      {tab === 'bluetooth' && (isLoading ? <BluetoothTabSkeleton /> : <LazyBluetoothTab />)}
      {tab === 'system' && (isLoading ? <SystemTabSkeleton /> : <LazySystemTab />)}
    </PageLayout>
  );
}
