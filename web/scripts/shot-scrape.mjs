#!/usr/bin/env node
/**
 * Pull shots off a live GaggiMate and print them in the compact LLM format.
 *
 * The device does no work for this: /api/history/<id>.slog is a static
 * LittleFS serve (WebUIPlugin.cpp), and the binary is already the most compact
 * representation there is -- 512 B header + 26 B/sample, ~5.7 KB for a 50 s
 * shot. Decoding and formatting happen here, reusing the same parsers and
 * analyzer the web UI runs, so output can't drift from what the UI shows.
 *
 *   node scripts/shot-scrape.mjs --list
 *   node scripts/shot-scrape.mjs 24
 *   node scripts/shot-scrape.mjs --last 5 --out ./shots
 *   node scripts/shot-scrape.mjs 24 --host 192.168.1.145
 */
import { writeFileSync, mkdirSync } from 'fs';
import { join } from 'path';
import { parseBinaryShot } from '../src/pages/ShotHistory/parseBinaryShot.js';
import { parseBinaryIndex, indexToShotList } from '../src/pages/ShotHistory/parseBinaryIndex.js';
import { buildShotText } from '../src/pages/ShotAnalyzer/services/shotTextExport.js';

const args = process.argv.slice(2);
const flag = (name, fallback = null) => {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};
const has = name => args.includes(name);

const HOST = flag('--host', process.env.GAGGIMATE_HOST || 'gaggimate.local');
const BASE = HOST.startsWith('http') ? HOST : `http://${HOST}`;
const pad = id => String(id).padStart(6, '0');

async function get(path, as = 'buffer') {
  const res = await fetch(`${BASE}${path}`);
  if (!res.ok) throw new Error(`GET ${path} -> HTTP ${res.status}`);
  if (as === 'json') return res.json();
  const buf = await res.arrayBuffer();
  // A miss falls through to the SPA index.html, which is gzipped -- catch that
  // rather than handing a web page to the binary parser.
  const head = new Uint8Array(buf.slice(0, 2));
  if (head[0] === 0x1f && head[1] === 0x8b) throw new Error(`${path} not found on device`);
  return buf;
}

async function listShots() {
  return indexToShotList(parseBinaryIndex(await get('/api/history/index.bin')));
}

/** Notes live beside the .slog and carry dose, rating and tasting text. */
async function fetchNotes(id) {
  try {
    return await get(`/api/history/${id}.json`, 'json');
  } catch {
    return null;
  }
}

/** Profile enables planned-vs-actual phase lines; best-effort via the REST API. */
async function fetchProfile(profileId) {
  if (!profileId) return null;
  try {
    return await get(`/api/profiles/${encodeURIComponent(profileId)}`, 'json');
  } catch {
    return null;
  }
}

async function scrape(id) {
  const shot = parseBinaryShot(await get(`/api/history/${pad(id)}.slog`), String(id));
  const [notes, profile] = await Promise.all([fetchNotes(id), fetchProfile(shot.profileId)]);
  return buildShotText(shot, profile, { notes });
}

const ids = args.filter(a => /^\d+$/.test(a));
const outDir = flag('--out');
const shots = await listShots();

if (has('--list') || (!ids.length && !has('--last'))) {
  console.log(`${shots.length} shots on ${HOST}:\n`);
  for (const s of shots.slice(0, Number(flag('--limit', '30')))) {
    const when = s.timestamp
      ? new Date(s.timestamp * 1000).toISOString().slice(0, 16).replace('T', ' ')
      : '';
    console.log(
      `  #${String(s.id).padStart(4)}  ${when}  ${String(s.profile || '')
        .slice(0, 28)
        .padEnd(28)}  ${s.volume ?? '?'} g`,
    );
  }
  console.log('\nnode scripts/shot-scrape.mjs <id>...  |  --last N  |  --out <dir>');
  process.exit(0);
}

const targets = ids.length ? ids : shots.slice(0, Number(flag('--last', '1'))).map(s => s.id);
if (outDir) mkdirSync(outDir, { recursive: true });

for (const id of targets) {
  try {
    const text = await scrape(id);
    if (!text) {
      console.error(`# shot ${id}: no sample data`);
      continue;
    }
    if (outDir) {
      const file = join(outDir, `shot-${pad(id)}.md`);
      writeFileSync(file, text);
      console.error(`wrote ${file} (${text.length} B)`);
    } else {
      process.stdout.write(text);
    }
  } catch (err) {
    console.error(`# shot ${id}: ${err.message}`);
  }
}
