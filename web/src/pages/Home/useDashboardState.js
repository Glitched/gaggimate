import { computed } from '@preact/signals';
import { useEffect, useState } from 'preact/hooks';
import { machine, prefetchSettings, getCachedSettings } from '../../services/ApiService.js';
import { machineApi } from '../../services/api.js';

const status = computed(() => machine.value.status);
const capabilities = computed(() => machine.value.capabilities);

export function useDashboardState() {
  const [isFlushing, setIsFlushing] = useState(false);

  const s = status.value;
  const caps = capabilities.value;
  const p = s.process;

  // Share the app-wide settings cache (the one the Settings page updates on
  // save) instead of a second useQuery cache. useQuery also takes no options
  // object, so the staleTime/refetch options previously passed were silently
  // ignored and every dashboard mount re-fetched from the device.
  const [settings, setSettings] = useState(() => getCachedSettings());
  useEffect(() => {
    let cancelled = false;
    prefetchSettings()
      .then(data => {
        if (!cancelled) setSettings(data);
      })
      .catch(error => {
        console.error('Failed to load settings for dashboard:', error);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  // ── derived ──────────────────────────────────────────────
  const isActive = !!p?.a;
  const isFinished = !!p?.e && !isActive;
  const isBrewing = s.mode === 1;
  const isGrinding = s.mode === 4;

  const isSmartGrindEnabled = settings?.smartGrindActive || false;
  const altRelayFunction = settings?.altRelayFunction ?? 1;
  const isGrindAvailable = isSmartGrindEnabled || altRelayFunction === 1;
  const showGrindTab = isGrindAvailable || isGrinding;

  // ── water level (Alba) ────────────────────────────────────
  const ledControl = caps?.ledControl || false;
  const emptyTankDistance = settings?.emptyTankDistance || 0;
  const fullTankDistance = settings?.fullTankDistance || 0;
  const albaCalibrated = emptyTankDistance > 0 && fullTankDistance > 0;
  const waterLevelPercent =
    ledControl && albaCalibrated
      ? Math.max(
          0,
          Math.min(
            100,
            ((emptyTankDistance - s.tofDistance) / (emptyTankDistance - fullTankDistance)) * 100,
          ),
        )
      : null;

  // ── handlers ─────────────────────────────────────────────
  // apiService.send throws when the socket is down; these run from click
  // handlers, so swallow the error instead of leaving it uncaught.
  // Commands go over HTTP (services/api.js); the socket only carries status.
  // Buttons fire and forget, so a failure is logged rather than thrown.
  const run = promise => promise.catch(error => console.warn('Command failed:', error.message));

  const changeMode = mode => run(machineApi.setMode(mode));

  const activate = () => run(isGrinding ? machineApi.grindActivate() : machineApi.activate());
  const deactivate = () => {
    run(isGrinding ? machineApi.grindDeactivate() : machineApi.deactivate());
    if (isFlushing) {
      run(machineApi.clear());
      setIsFlushing(false);
    }
  };
  const clear = () => {
    run(machineApi.clear());
    setIsFlushing(false);
  };

  const startFlush = () => {
    if (isFlushing) return;
    setIsFlushing(true);
    machineApi.flush().catch(() => setIsFlushing(false));
  };

  const raiseTemp = () => run(machineApi.step('temperature', 'raise'));
  const lowerTemp = () => run(machineApi.step('temperature', 'lower'));
  const raiseTarget = () => run(machineApi.step(isGrinding ? 'grind' : 'brew', 'raise'));
  const lowerTarget = () => run(machineApi.step(isGrinding ? 'grind' : 'brew', 'lower'));
  // TargetToggle passes 0/1: time-based vs volumetric targets.
  const changeTarget = target => run(machineApi.setVolumetric(!!target));

  return {
    // raw status
    mode: s.mode,
    currentTemperature: s.currentTemperature,
    targetTemperature: s.targetTemperature,
    currentPressure: s.currentPressure,
    targetPressure: s.targetPressure,
    currentFlow: s.currentFlow,
    targetFlow: s.targetFlow,
    currentWeight: s.currentWeight,
    currentPumpPower: s.currentPumpPower ?? 0,
    currentBoilerPower: s.currentBoilerPower ?? 0,
    currentPuckResistance: s.currentPuckResistance ?? 0,
    currentPuckFlow: s.currentPuckFlow ?? 0,
    currentCoffeeVolume: s.currentCoffeeVolume ?? 0,
    targetWeight: s.targetWeight,
    volumetricAvailable: s.volumetricAvailable,
    brewTarget: !!s.brewTarget,
    grindTarget: s.grindTarget,
    grindTargetDuration: s.grindTargetDuration,
    grindTargetVolume: s.grindTargetVolume,
    selectedProfile: s.selectedProfile,
    selectedProfileId: s.selectedProfileId,
    processInfo: p,
    tofDistance: s.tofDistance,
    // derived
    isActive,
    isFinished,
    isBrewing,
    isGrinding,
    isGrindAvailable,
    showGrindTab,
    // water level
    ledControl,
    albaCalibrated,
    waterLevelPercent,
    // settings
    isSmartGrindEnabled,
    altRelayFunction,
    emptyTankDistance,
    fullTankDistance,
    // action state
    isFlushing,
    // handlers
    changeMode,
    activate,
    deactivate,
    clear,
    startFlush,
    raiseTemp,
    lowerTemp,
    raiseTarget,
    lowerTarget,
    changeTarget,
  };
}
