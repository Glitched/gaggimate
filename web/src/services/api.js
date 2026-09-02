// HTTP client for the device API (docs/http-api.yaml).
//
// Commands and queries go over plain HTTP; the WebSocket (ApiService) carries
// only the device's pushes (`evt:*`). Same origin as the page — the Vite dev
// server proxies /api to the simulator or a real machine.
//
// Every helper returns the parsed JSON body and throws an ApiError carrying the
// status and the device's `error` string on a non-2xx response, so callers get
// "Profile not found" rather than a generic failure.

export class ApiError extends Error {
  constructor(message, status, body) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.body = body;
  }
}

// A stalled request is aborted after this long so callers never hang on a dead
// connection (matches prefetchSettings in ApiService). Pass `signal` in the
// options to use your own — the caller's signal replaces the timeout.
const DEFAULT_TIMEOUT_MS = 15000;

async function call(method, path, body, { signal, timeoutMs = DEFAULT_TIMEOUT_MS } = {}) {
  const init = { method, headers: {} };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  init.signal =
    signal ??
    (typeof AbortSignal.timeout === 'function' ? AbortSignal.timeout(timeoutMs) : undefined);
  let res;
  let text;
  try {
    res = await fetch(path, init);
    text = await res.text();
  } catch (e) {
    // Surface a readable reason instead of a bare AbortError/TimeoutError name.
    if (e?.name === 'TimeoutError' || (e?.name === 'AbortError' && !signal)) {
      throw new ApiError('Request timed out', 0, null);
    }
    if (e?.name === 'AbortError') throw new ApiError('Request cancelled', 0, null);
    throw e;
  }
  let data = null;
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = text;
    }
  }
  if (!res.ok) {
    const message = (data && typeof data === 'object' && data.error) || `HTTP ${res.status}`;
    throw new ApiError(message, res.status, data);
  }
  return data;
}

const enc = encodeURIComponent;

export const http = {
  get: (path, options) => call('GET', path, undefined, options),
  post: (path, body, options) => call('POST', path, body, options),
  put: (path, body, options) => call('PUT', path, body, options),
  del: (path, options) => call('DELETE', path, undefined, options),
};

export const profilesApi = {
  /** Array of profiles; `minimal` returns id, label, favorite and selected only (no phases). */
  list: ({ minimal = false } = {}) =>
    http.get(`/api/profiles${minimal ? '?minimal=1' : ''}`).then(d => d?.profiles ?? []),
  load: id => http.get(`/api/profiles/${enc(id)}`),
  /**
   * Upsert: PUT when the profile carries an id, falling back to POST if the
   * device has never seen that id (imports, the calibration profile). Returns
   * the stored profile.
   */
  save: async profile => {
    if (profile.id) {
      try {
        return await http.put(`/api/profiles/${enc(profile.id)}`, profile);
      } catch (e) {
        if (!(e instanceof ApiError) || e.status !== 404) throw e;
      }
    }
    return http.post('/api/profiles', profile);
  },
  remove: id => http.del(`/api/profiles/${enc(id)}`),
  select: id => http.post(`/api/profiles/${enc(id)}/select`),
  favorite: id => http.post(`/api/profiles/${enc(id)}/favorite`),
  unfavorite: id => http.post(`/api/profiles/${enc(id)}/unfavorite`),
  reorder: ids => http.post('/api/profiles/reorder', { ids }),
};

export const historyApi = {
  /** Notes object, or {} when the shot has none (the file simply doesn't exist). */
  notes: async id => {
    try {
      const data = await http.get(`/api/history/${enc(id)}.json`);
      return data && typeof data === 'object' ? data : {};
    } catch (e) {
      if (e instanceof ApiError && e.status === 404) return {};
      throw e;
    }
  },
  saveNotes: (id, notes) => http.put(`/api/history/${enc(id)}.json`, notes),
  remove: id => http.del(`/api/history/${enc(id)}`),
  rebuild: () => http.post('/api/history/rebuild'),
};

export const machineApi = {
  /** mode: 0–4 or 'standby' | 'brew' | 'steam' | 'water' | 'grind'. */
  setMode: mode => http.post('/api/mode', { mode }),
  activate: () => http.post('/api/process/activate'),
  deactivate: () => http.post('/api/process/deactivate'),
  clear: () => http.post('/api/process/clear'),
  grindActivate: () => http.post('/api/grind/activate'),
  grindDeactivate: () => http.post('/api/grind/deactivate'),
  flush: () => http.post('/api/flush'),
  autotune: ({ time, samples, wattage }) => http.post('/api/autotune', { time, samples, wattage }),
  /** target: 'temperature' | 'brew' | 'grind'; direction: 'raise' | 'lower'. */
  step: (target, direction) => http.post(`/api/targets/${target}/${direction}`),
  /** Absolute value for 'temperature' (°C) or 'grind' (g). */
  setTarget: (target, value) => http.put(`/api/targets/${target}`, { value }),
  /** Time-based (false) or volumetric (true) brew/grind targets. */
  setVolumetric: volumetric => http.put('/api/targets/mode', { volumetric }),
};

export const otaApi = {
  status: () => http.get('/api/ota'),
  /** Re-check for updates, optionally switching channel first. Result arrives as evt:ota-status on the socket. */
  check: channel => http.post('/api/ota', channel ? { channel } : {}),
  start: component => http.post('/api/ota/start', { component }),
};
