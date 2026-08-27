/**
 * Token-efficient shot export for LLM consumption.
 *
 * Emits YAML frontmatter (everything that is per-shot or per-phase) followed by
 * a bare CSV body (everything that is per-sample). The current JSON export costs
 * ~35k tokens for a 50 s shot; ~93% of that is packaging -- repeated key names,
 * pretty-print indentation, and a `systemInfo` object re-serialised for every
 * sample despite taking only a handful of distinct values across a whole shot.
 * Same data in this format is ~5.7k tokens at the full recording rate.
 *
 * The summary half is deliberately verbose: phases are few (<=12) and every line
 * is diagnostic, so labelled keys earn their tokens. The sample half is
 * deliberately terse: rows are many and each one is low-value, so keys appear
 * once in a header row.
 */
import { calculateShotMetrics } from './analyzer/shotAnalysis.js';
import { PHASE_EXIT_REASON } from './analyzer/exitReasons.js';

const DEFAULT_COLUMNS = ['sec', 'bar', 'tbar', 'flow', 'puck', 'weight', 'temp'];

// Sample key + decimal places for every column the body can carry.
const COLUMN_SPEC = {
  bar: { key: 'cp', dp: 1 },
  tbar: { key: 'tp', dp: 0 },
  flow: { key: 'fl', dp: 1 },
  tflow: { key: 'tf', dp: 1 },
  puck: { key: 'pf', dp: 1 },
  weight: { key: 'v', dp: 1 },
  est_weight: { key: 'ev', dp: 1 },
  weight_flow: { key: 'vf', dp: 2 },
  temp: { key: 'ct', dp: 1 },
  resist: { key: 'pr', dp: 2 },
};

function num(value, dp = 1) {
  if (value == null || Number.isNaN(Number(value))) return null;
  return Number(Number(value).toFixed(dp));
}

/** Fixed-decimal string, so a series never mixes "4" and "4.0". */
function fixed(value, dp = 1) {
  const n = num(value, dp);
  return n == null ? null : n.toFixed(dp);
}

/** Quote only when a bare YAML scalar would be ambiguous. */
function yamlScalar(value) {
  if (value == null || value === '') return 'null';
  const s = String(value);
  if (/^\d+$/.test(s)) return s;
  if (/^[\w .+/()&'-]+$/.test(s) && !/^\s|\s$/.test(s) && !/^(true|false|null|~|-?\d)/i.test(s)) {
    return s;
  }
  return JSON.stringify(s);
}

function yamlLine(key, value, comment) {
  const body = `${key}: ${value}`;
  return comment ? `${body.padEnd(30)}# ${comment}` : body;
}

function formatLocalTimestamp(epochSeconds) {
  if (!epochSeconds) return null;
  const d = new Date(epochSeconds * 1000);
  if (d.getFullYear() <= 1970) return null;
  const p = n => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

/** "3.3 -> 0.7" plus a min/max comment when the range exceeds the endpoints. */
function rangeLine(key, stats, dp = 1) {
  if (!stats) return null;
  const start = fixed(stats.start, dp);
  const end = fixed(stats.end, dp);
  const min = fixed(stats.min, dp);
  const max = fixed(stats.max, dp);
  const excursion =
    Number(min) < Math.min(Number(start), Number(end)) ||
    Number(max) > Math.max(Number(start), Number(end));
  const comment = excursion ? `min ${min} max ${max}` : null;
  return yamlLine(`    ${key}`, `${start} -> ${end}`, comment);
}

/**
 * Phase exit, with planned-vs-actual when the profile is available.
 * This is the single most diagnostic line in the export: it is what tells you a
 * phase was cut short by a weight stop rather than running its planned duration.
 */
function exitLine(phase) {
  const reason = phase.exit?.reason || phase.stats?.sys_recorded_stop_reason;
  const code = phase.exit?.code;
  const actual = num(phase.duration, 1);
  const planned = num(phase.profilePhase?.duration, 0);

  if (!reason || code === PHASE_EXIT_REASON.UNKNOWN) {
    // Pre-v5 firmware, or a v5 file written before transitionReason was populated.
    return yamlLine('    ended', 'not recorded', `ran ${actual}s`);
  }
  if (planned == null) {
    return yamlLine('    ended', yamlScalar(reason), `${actual}s (no profile to compare)`);
  }
  const delta = num(actual - planned, 1);
  const sign = delta > 0 ? `+${delta}` : `${delta}`;
  return yamlLine('    ended', yamlScalar(reason), `${actual}s of ${planned}s planned (${sign})`);
}

function buildFrontmatter(shot, results, notes, profileData) {
  const lines = [];
  const dose = num(notes?.doseIn, 1);
  const yield_ = num(results.total?.weight ?? shot.volume, 1);
  const duration = num(results.total?.duration, 1);

  lines.push(yamlLine('shot', yamlScalar(shot.id)));
  lines.push(yamlLine('profile', yamlScalar(shot.profile || 'Unknown')));
  const at = formatLocalTimestamp(shot.timestamp);
  if (at) lines.push(yamlLine('at', at));
  if (results.brewModeLabel) {
    lines.push(yamlLine('brew_mode', yamlScalar(results.brewModeLabel)));
  }
  lines.push(yamlLine('outcome', `${yield_} g in ${duration} s`));
  if (results.finalExitReasonLabel && results.finalExitReasonCode !== PHASE_EXIT_REASON.UNKNOWN) {
    lines.push(yamlLine('stopped_by', yamlScalar(results.finalExitReasonLabel)));
  }

  if (dose != null && dose > 0) {
    lines.push(yamlLine('dose_g', dose));
    if (yield_ != null && yield_ > 0) {
      lines.push(yamlLine('ratio', `1:${num(yield_ / dose, 1)}`));
    }
  } else {
    lines.push(yamlLine('dose_g', 'null', 'NOT RECORDED -> ratio/EY cannot be computed'));
  }

  const targetTemp = results.total?.tt;
  if (targetTemp) {
    const constant = num(targetTemp.min, 1) === num(targetTemp.max, 1);
    lines.push(
      constant
        ? yamlLine('target_temp_c', num(targetTemp.avg, 1), 'held constant for the whole shot')
        : yamlLine('target_temp_c', `${num(targetTemp.start, 1)} -> ${num(targetTemp.end, 1)}`),
    );
  }
  if (notes?.beanType) lines.push(yamlLine('beans', yamlScalar(notes.beanType)));
  if (notes?.grindSetting) lines.push(yamlLine('grind', yamlScalar(notes.grindSetting)));
  if (notes?.balanceTaste) lines.push(yamlLine('taste', yamlScalar(notes.balanceTaste)));
  if (notes?.rating) lines.push(yamlLine('rating', `${notes.rating}/5`));
  if (notes?.notes) lines.push(yamlLine('tasting_notes', yamlScalar(notes.notes)));
  if (!profileData) {
    lines.push(
      yamlLine('profile_targets', 'unavailable', 'profile not loaded; no planned-vs-actual'),
    );
  }
  if (shot.incomplete) {
    lines.push(yamlLine('incomplete', 'true', 'recording was truncated'));
  }

  lines.push('');
  lines.push('phases:');
  for (const phase of results.phases || []) {
    if (phase.skipped) continue;
    lines.push(`  - n: ${Number(phase.number) + 1}`);
    lines.push(`    name: ${yamlScalar(phase.displayName || phase.name)}`);
    lines.push(exitLine(phase));
    const water = fixed(phase.water, 1);
    if (water != null) lines.push(yamlLine('    water_ml', water));
    const weight = fixed(phase.weight, 1);
    if (weight != null) lines.push(yamlLine('    weight_g', weight));
    for (const [label, statKey] of [
      ['bar', 'p'],
      ['flow', 'f'],
      ['puck_flow', 'pf'],
    ]) {
      const line = rangeLine(label, phase.stats?.[statKey]);
      if (line) lines.push(line);
    }
    const temp = phase.stats?.t;
    if (temp) lines.push(yamlLine('    temp_avg', fixed(temp.avg, 1)));
  }

  const total = results.total;
  if (total) {
    lines.push('');
    lines.push('total:');
    lines.push(yamlLine('  duration_s', num(total.duration, 1)));
    lines.push(yamlLine('  water_ml', num(total.water, 1)));
    lines.push(yamlLine('  weight_g', num(total.weight, 1)));
    if (total.t) lines.push(yamlLine('  temp_avg', num(total.t.avg, 1)));
  }

  const warnings = [];
  if (results.globalScaleLost) warnings.push('bluetooth scale dropped out mid-shot');
  if (results.highScaleDelay) {
    warnings.push(`high scale latency (~${Math.round(results.highScaleDelayMs || 0)} ms)`);
  }
  if (results.delayReviewMessage) warnings.push(results.delayReviewMessage);
  if (warnings.length) {
    lines.push('');
    lines.push('warnings:');
    for (const w of warnings) lines.push(`  - ${yamlScalar(w)}`);
  }

  return lines.join('\n');
}

function buildCsv(samples, phases, columns) {
  const startT = samples[0]?.t ?? 0;
  // First sample index of each phase -> divider label.
  const dividerAt = new Map();
  for (const phase of phases || []) {
    if (phase.skipped) continue;
    const idx = samples.findIndex(s => (s.t - startT) / 1000 >= phase.start - 1e-6);
    if (idx >= 0 && !dividerAt.has(idx)) {
      dividerAt.set(idx, phase.displayName || phase.name);
    }
  }

  const rows = [columns.join(',')];
  samples.forEach((sample, i) => {
    if (dividerAt.has(i)) rows.push(`# --- ${dividerAt.get(i)} ---`);
    const cells = columns.map(col => {
      if (col === 'sec') return ((sample.t - startT) / 1000).toFixed(2);
      const spec = COLUMN_SPEC[col];
      if (!spec) return '';
      const v = num(sample[spec.key], spec.dp);
      return v == null ? '' : v.toFixed(spec.dp);
    });
    rows.push(cells.join(','));
  });
  return rows.join('\n');
}

/**
 * @param {Object} shot        parseBinaryShot() result (must have .samples)
 * @param {Object|null} profileData  profile for planned-vs-actual, optional
 * @param {Object} [options]
 * @param {Object|null} [options.notes]  shot notes (dose, rating, tasting)
 * @param {string[]} [options.columns]   body columns, see COLUMN_SPEC
 * @returns {string} frontmatter + CSV
 */
export function buildShotText(shot, profileData = null, options = {}) {
  const { notes = null, columns = DEFAULT_COLUMNS } = options;
  if (!shot || !Array.isArray(shot.samples) || shot.samples.length === 0) {
    return null;
  }

  const brewDelay = Math.max(0, Number(shot.brewDelay) || 0);
  const results = calculateShotMetrics(shot, profileData, {
    scaleDelayMs: brewDelay,
    sensorDelayMs: brewDelay,
    isAutoAdjusted: false,
    ...(options.settings || {}),
  });

  const frontmatter = buildFrontmatter(shot, results, notes || shot.notes, profileData);
  const samples = shot.samples;
  const csv = buildCsv(samples, results.phases, columns);
  const intervalMs = samples.length > 1 ? Math.round(samples[1].t - samples[0].t) : 0;

  return `---\n${frontmatter}\n---\n# ${samples.length} samples, ${intervalMs} ms apart\n${csv}\n`;
}

export { DEFAULT_COLUMNS, COLUMN_SPEC };
