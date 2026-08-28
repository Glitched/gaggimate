import Card from '../../components/Card.jsx';
import { useCallback, useEffect, useRef, useState, useContext } from 'preact/hooks';
import { HistoryChart } from './HistoryChart.jsx';
import { downloadJson, downloadText } from '../../utils/download.js';
import { showToast } from '../../services/toast.js';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faTrashCan } from '@fortawesome/free-solid-svg-icons/faTrashCan';
import { faWeightScale } from '@fortawesome/free-solid-svg-icons/faWeightScale';
import { faClock } from '@fortawesome/free-solid-svg-icons/faClock';
import { faUpload } from '@fortawesome/free-solid-svg-icons/faUpload';
import { faStar } from '@fortawesome/free-solid-svg-icons/faStar';
import { faPlus } from '@fortawesome/free-solid-svg-icons/faPlus';
import { faMinus } from '@fortawesome/free-solid-svg-icons/faMinus';
import { faMagnifyingGlassChart } from '@fortawesome/free-solid-svg-icons/faMagnifyingGlassChart';
import { faRobot } from '@fortawesome/free-solid-svg-icons/faRobot';
import { faCheck } from '@fortawesome/free-solid-svg-icons/faCheck';
import { faEllipsis } from '@fortawesome/free-solid-svg-icons/faEllipsis';
import { buildShotText } from '../ShotAnalyzer/services/shotTextExport.js';
import ShotNotesCard from './ShotNotesCard.jsx';
import { useConfirmAction } from '../../hooks/useConfirmAction.js';

import VisualizerUploadModal from '../../components/VisualizerUploadModal.jsx';
import { visualizerService } from '../../services/VisualizerService.js';
import { ApiServiceContext } from '../../services/ApiService.js';
import { Tooltip } from '../../components/Tooltip.jsx';

function round2(v) {
  if (v == null || Number.isNaN(v)) return v;
  return Math.round((v + Number.EPSILON) * 100) / 100;
}

export default function HistoryCard({ shot, onDelete, onLoad, onNotesChanged }) {
  const apiService = useContext(ApiServiceContext);
  const [shotNotes, setShotNotes] = useState(shot.notes || null);
  const [expanded, setExpanded] = useState(false);
  const { armed: confirmDelete, armOrRun: confirmOrDelete } = useConfirmAction(4000);
  const [showUploadModal, setShowUploadModal] = useState(false);
  const [copiedForLlm, setCopiedForLlm] = useState(false);
  // True while an action is fetching the shot it needs.
  const [loading, setLoading] = useState(false);
  const [isUploading, setIsUploading] = useState(false);
  const copiedTimerRef = useRef(null);
  useEffect(() => () => clearTimeout(copiedTimerRef.current), []);

  const date = new Date(shot.timestamp * 1000);

  // Clicking an action used to be refused until you had expanded the row to
  // load the shot -- the button sat disabled behind a "Load first" tooltip,
  // making you run an errand before the thing you asked for. Now the action
  // does the errand itself: expand, fetch if needed, then carry on with the
  // freshly loaded shot (which the parent hands back, since our own `shot` prop
  // is still the unloaded one until the re-render lands).
  const ensureLoaded = useCallback(async () => {
    if (shot.loaded) return shot;
    setExpanded(true);
    setLoading(true);
    try {
      return (await onLoad?.(shot.id)) || null;
    } finally {
      setLoading(false);
    }
  }, [shot, onLoad]);

  const onExport = useCallback(async () => {
    const loaded = await ensureLoaded();
    if (!loaded) return;
    const exportData = { ...loaded, notes: shotNotes };
    if (Array.isArray(exportData.samples)) {
      exportData.samples = exportData.samples.map(s => ({
        t: s.t,
        tt: round2(s.tt),
        ct: round2(s.ct),
        tp: round2(s.tp),
        cp: round2(s.cp),
        fl: round2(s.fl),
        tf: round2(s.tf),
        pf: round2(s.pf),
        vf: round2(s.vf),
        v: round2(s.v),
        ev: round2(s.ev),
        pr: round2(s.pr),
        systemInfo: s.systemInfo,
        phaseNumber: s.phaseNumber,
        phaseDisplayNumber: s.phaseDisplayNumber,
      }));
    }
    exportData.volume = round2(exportData.volume);
    // duration left as integer ms
    downloadJson(exportData, 'shot-' + loaded.id + '.json');
  }, [ensureLoaded, shotNotes]);

  // Compact text export (YAML frontmatter + CSV) sized for pasting into an LLM.
  // ~6% the tokens of the JSON export with no samples dropped; see shotTextExport.js.
  const onCopyForLlm = useCallback(async () => {
    const loaded = await ensureLoaded();
    if (!loaded) return;
    let profileData = null;
    if (loaded.profileId && apiService) {
      try {
        const res = await apiService.request({ tp: 'req:profiles:load', id: loaded.profileId });
        if (res.profile) profileData = res.profile;
      } catch (error) {
        // Planned-vs-actual is then omitted; the rest of the export is unaffected.
        console.warn('Failed to fetch profile for LLM export:', error);
      }
    }
    const text = buildShotText(loaded, profileData, { notes: shotNotes });
    if (!text) return;
    try {
      await navigator.clipboard.writeText(text);
      setCopiedForLlm(true);
      clearTimeout(copiedTimerRef.current);
      copiedTimerRef.current = setTimeout(() => setCopiedForLlm(false), 2000);
    } catch (error) {
      console.error('Clipboard write failed, falling back to download:', error);
      downloadText(text, 'shot-' + loaded.id + '.md');
    }
  }, [ensureLoaded, shotNotes, apiService]);

  const handleNotesLoaded = useCallback(notes => {
    setShotNotes(notes);
  }, []);

  const handleNotesUpdate = useCallback(
    notes => {
      setShotNotes(notes);
      // Notify parent that notes changed (so it can reload the index)
      if (onNotesChanged) onNotesChanged();
    },
    [onNotesChanged],
  );
  const profileTitle = shot.profile || 'Unknown Profile';
  let formattedDate = 'No timestamp available';
  if (date.getFullYear() > 1970) {
    const sameYear = date.getFullYear() === new Date().getFullYear();
    formattedDate =
      date.toLocaleDateString([], {
        month: 'short',
        day: 'numeric',
        ...(sameYear ? {} : { year: 'numeric' }),
      }) +
      ', ' +
      date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }

  const handleUpload = useCallback(
    async (username, password, rememberCredentials) => {
      setIsUploading(true);
      try {
        // Validate shot data
        if (!visualizerService.validateShot(shot)) {
          throw new Error('Shot data is invalid or incomplete');
        }

        // Fetch profile data if profileId is available
        let profileData = null;
        if (shot.profileId && apiService) {
          try {
            const profileResponse = await apiService.request({
              tp: 'req:profiles:load',
              id: shot.profileId,
            });
            if (profileResponse.profile) {
              profileData = profileResponse.profile;
            }
          } catch (error) {
            console.warn('Failed to fetch profile data:', error);
            // Continue without profile data
          }
        }

        // Include notes in shot data
        const shotWithNotes = {
          ...shot,
          notes: shotNotes,
        };

        await visualizerService.uploadShot(shotWithNotes, username, password, profileData);

        // Show success message
        showToast('Shot uploaded to visualizer.coffee', { type: 'success' });
      } catch (error) {
        console.error('Upload failed:', error);
        showToast(`Upload failed: ${error.message}`, { type: 'error' });
        throw error; // Re-throw to prevent modal from closing
      } finally {
        setIsUploading(false);
      }
    },
    [shot, shotNotes, apiService],
  );

  // validateShot needs the samples, which only exist once the shot is fetched,
  // so this is always false beforehand -- gating the button on it made this the
  // same errand as the other actions. Load first, then judge.
  const onUploadClick = useCallback(async () => {
    const loaded = await ensureLoaded();
    if (!loaded) return;
    if (!visualizerService.validateShot(loaded)) {
      showToast('This shot has no data to upload', { type: 'error' });
      return;
    }
    setShowUploadModal(true);
  }, [ensureLoaded]);

  // Rendered in two places (inline with the title from sm up, on its own row
  // below it on a phone), so it lives here rather than being duplicated.
  // Fixed widths from sm up so the metrics line up as columns down the list --
  // otherwise the group's width follows its content ("0.2g" vs "64.3g") and
  // every row starts at a slightly different x. tabular-nums keeps the digits
  // themselves on a grid. Left unconstrained on phones, where they wrap under
  // the title and a fixed width would only waste space.
  const metrics = (
    <>
      <div className='flex items-center gap-1 sm:w-20'>
        <FontAwesomeIcon icon={faClock} className='h-4 w-4 shrink-0' />
        <span className='tabular-nums'>{(shot.duration / 1000).toFixed(1)}s</span>
      </div>

      <div className='flex items-center gap-1 sm:w-20'>
        {shot.volume && shot.volume > 0 ? (
          <>
            <FontAwesomeIcon icon={faWeightScale} className='h-4 w-4 shrink-0' />
            <span className='tabular-nums'>{round2(shot.volume)}g</span>
          </>
        ) : null}
      </div>

      {shot.rating && shot.rating > 0 ? (
        <div className='flex items-center gap-1 sm:w-24'>
          <FontAwesomeIcon icon={faStar} className='h-4 w-4 shrink-0 text-yellow-500' />
          <span className='font-medium tabular-nums'>{shot.rating}/5</span>
        </div>
      ) : (
        <div className='text-base-content/50 flex items-center gap-1 sm:w-24'>
          <FontAwesomeIcon icon={faStar} className='h-4 w-4 shrink-0' />
          <span>Not rated</span>
        </div>
      )}
    </>
  );

  return (
    <Card sm={12} className='[&>.card-body]:p-2'>
      <div className='flex flex-col gap-2'>
        <div className='flex flex-row items-start gap-2'>
          {/* No border: an outlined box against the card's own outline was two
              competing frames, and it never quite agreed with the card's corner.
              The radius still matters for the hover fill -- rounded-lg (8px) is
              the card's --radius-box (16px) less its p-2 body padding, which is
              what makes a nested corner look concentric. */}
          <button
            className='text-base-content/60 hover:text-base-content hover:bg-base-content/10 cursor-pointer rounded-lg p-2 transition-colors'
            onClick={() => {
              const next = !expanded;
              setExpanded(next);
              if (next && !shot.loaded && onLoad) onLoad(shot.id);
            }}
            aria-label={expanded ? 'Collapse shot details' : 'Expand shot details'}
          >
            <FontAwesomeIcon icon={expanded ? faMinus : faPlus} className='h-3 w-3' />
          </button>

          <div className='min-w-0 flex-grow'>
            {/* Header Row */}
            {/* items-start on phones keeps the ⋯ level with the title; from sm
                up everything centres against the two-line title block so the
                metrics sit in the middle of the row rather than on its top line. */}
            <div className='mb-1 flex flex-row items-start justify-between gap-3 sm:items-center'>
              <div className='min-w-0 flex-grow'>
                <h3 className='text-base-content truncate text-base font-semibold'>
                  {profileTitle}
                </h3>
                <p className='text-base-content/70 text-sm whitespace-nowrap'>
                  #{shot.id} • {formattedDate}
                </p>
                {expanded &&
                  shot.loaded &&
                  shot.samples &&
                  shot.samples.length > 0 &&
                  shot.samples[0].systemInfo && (
                    <p className='text-base-content/60 text-xs italic'>
                      Brewed by{' '}
                      {shot.samples[0].systemInfo.shotStartedVolumetric ? 'Weight' : 'Time'}
                    </p>
                  )}
              </div>

              {/* From sm up the metrics sit on the title line, which is what
                  fills the empty middle the stacked layout left across the card. */}
              <div className='text-base-content/80 hidden shrink-0 flex-row items-center gap-4 text-sm sm:flex'>
                {metrics}
              </div>

              <div className='flex shrink-0 flex-row items-center gap-2'>
                {shot.incomplete && (
                  <span className='inline-flex items-center rounded-full bg-yellow-100 px-2 py-1 text-xs font-medium text-yellow-800'>
                    INCOMPLETE
                  </span>
                )}

                <div className='hidden flex-row gap-1 sm:flex'>
                  <Tooltip content='Export'>
                    <button
                      disabled={loading}
                      onClick={onExport}
                      className='text-base-content/50 hover:text-info hover:bg-info/10 cursor-pointer rounded-md p-2 transition-colors disabled:cursor-not-allowed disabled:opacity-40'
                      aria-label='Export shot data'
                    >
                      <FontAwesomeIcon icon={faFileExport} className='h-4 w-4' />
                    </button>
                  </Tooltip>

                  {/* Copy for LLM */}
                  <Tooltip content='Copy for LLM'>
                    <button
                      disabled={loading}
                      onClick={onCopyForLlm}
                      className='text-base-content/50 hover:text-success hover:bg-success/10 cursor-pointer rounded-md p-2 transition-colors disabled:cursor-not-allowed disabled:opacity-40'
                      aria-label='Copy shot summary for an LLM'
                    >
                      <FontAwesomeIcon
                        icon={copiedForLlm ? faCheck : faRobot}
                        className={`h-4 w-4 ${copiedForLlm ? 'text-success' : ''}`}
                      />
                    </button>
                  </Tooltip>

                  {/* Analyzer Button */}
                  <Tooltip content='Open in Analyzer'>
                    <a
                      href={`/analyzer/internal/${shot.id}`}
                      className='text-base-content/50 hover:text-primary hover:bg-primary/10 flex items-center justify-center rounded-md p-2 transition-colors'
                      aria-label='Open in Analyzer'
                    >
                      <FontAwesomeIcon icon={faMagnifyingGlassChart} className='h-4 w-4' />
                    </a>
                  </Tooltip>

                  <Tooltip content='Upload to Visualizer.coffee'>
                    <button
                      onClick={onUploadClick}
                      disabled={loading}
                      className='text-success/70 hover:text-success hover:bg-success/10 cursor-pointer rounded-md px-2.5 py-2 text-sm transition-colors disabled:opacity-40'
                      aria-label='Upload to visualizer.coffee'
                    >
                      <FontAwesomeIcon icon={faUpload} />
                    </button>
                  </Tooltip>
                  <Tooltip content={confirmDelete ? 'Click to confirm delete' : 'Delete'}>
                    <button
                      onClick={() => {
                        confirmOrDelete(() => onDelete(shot.id));
                      }}
                      className={`cursor-pointer rounded-md p-2 transition-colors ${confirmDelete ? 'bg-error text-error-content font-semibold' : 'text-base-content/50 hover:text-error hover:bg-error/10'}`}
                      aria-label={confirmDelete ? 'Confirm deletion of shot' : 'Delete shot'}
                    >
                      <FontAwesomeIcon icon={faTrashCan} className='h-4 w-4' />
                      {confirmDelete && <span className='ml-2 hidden sm:inline'>Confirm</span>}
                    </button>
                  </Tooltip>
                </div>

                {/* Five icon buttons took a whole row on a phone and squeezed
                    the title until it truncated. Behind a menu they cost one
                    button, and each gets a name instead of a tooltip a touch
                    device never shows. */}
                <div className='dropdown dropdown-end sm:hidden'>
                  <div
                    tabIndex={0}
                    role='button'
                    className='text-base-content/50 hover:text-base-content hover:bg-base-content/10 cursor-pointer rounded-lg p-2 transition-colors'
                    aria-label='More actions for this shot'
                  >
                    <FontAwesomeIcon icon={faEllipsis} className='h-4 w-4' />
                  </div>
                  <ul
                    tabIndex={0}
                    className='dropdown-content menu bg-base-100 rounded-box border-base-300 z-10 mt-1 w-56 border p-2 shadow-lg'
                  >
                    <li>
                      <a href={`/analyzer/internal/${shot.id}`} className='justify-start'>
                        <FontAwesomeIcon icon={faMagnifyingGlassChart} className='h-4 w-4' />
                        <span>Open in Analyzer</span>
                      </a>
                    </li>
                    <li>
                      <button onClick={onExport} disabled={loading} className='justify-start'>
                        <FontAwesomeIcon icon={faFileExport} className='h-4 w-4' />
                        <span>Export</span>
                      </button>
                    </li>
                    <li>
                      <button onClick={onCopyForLlm} disabled={loading} className='justify-start'>
                        <FontAwesomeIcon
                          icon={copiedForLlm ? faCheck : faRobot}
                          className={`h-4 w-4 ${copiedForLlm ? 'text-success' : ''}`}
                        />
                        <span>{copiedForLlm ? 'Copied' : 'Copy for LLM'}</span>
                      </button>
                    </li>
                    <li>
                      <button onClick={onUploadClick} disabled={loading} className='justify-start'>
                        <FontAwesomeIcon icon={faUpload} className='h-4 w-4' />
                        <span>Upload to Visualizer</span>
                      </button>
                    </li>
                    <li>
                      <button
                        onClick={() => confirmOrDelete(() => onDelete(shot.id))}
                        className={`justify-start ${confirmDelete ? 'bg-error text-error-content font-semibold' : 'text-error'}`}
                      >
                        <FontAwesomeIcon icon={faTrashCan} className='h-4 w-4' />
                        <span>{confirmDelete ? 'Tap again to confirm' : 'Delete shot'}</span>
                      </button>
                    </li>
                  </ul>
                </div>
              </div>
            </div>

            {/* Phone-only: from sm up these sit on the title line above. */}
            <div className='text-base-content/80 mb-1 flex flex-row items-center gap-4 text-sm sm:hidden'>
              {metrics}
            </div>

            {expanded && (
              <div className='border-base-content/20 mt-4 border-t pt-4'>
                {!shot.loaded && (
                  <div className='flex items-center justify-center py-8'>
                    <span className='text-base-content/70 text-sm'>Loading shot data...</span>
                  </div>
                )}
                {shot.loaded && <HistoryChart shot={shot} />}
                {shot.loaded && (
                  <ShotNotesCard
                    shot={shot}
                    onNotesLoaded={handleNotesLoaded}
                    onNotesUpdate={handleNotesUpdate}
                  />
                )}
              </div>
            )}
          </div>
        </div>
      </div>

      <VisualizerUploadModal
        isOpen={showUploadModal}
        onClose={() => setShowUploadModal(false)}
        onUpload={handleUpload}
        isUploading={isUploading}
        shotInfo={{
          profile: shot.profile,
          timestamp: shot.timestamp,
          duration: shot.duration,
          volume: shot.volume,
        }}
      />
    </Card>
  );
}
