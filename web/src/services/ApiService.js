import { createContext } from 'preact';
import { signal } from '@preact/signals';
import uuidv4 from '../utils/uuid.js';

// The firmware pushes evt:status every 500ms, so a quiet connection is a dead
// one. Half-open sockets (phone sleep, Wi-Fi drop without FIN, device brownout)
// never fire `close`, which would otherwise leave the UI showing stale "live"
// data indefinitely.
const LIVENESS_TIMEOUT_MS = 10000;
const LIVENESS_CHECK_INTERVAL_MS = 2500;

export default class ApiService {
  socket = null;
  listeners = {};
  listenerIdCounter = 0;
  pendingRequests = new Map();
  reconnectAttempts = 0;
  maxReconnectDelay = 30000; // Maximum delay of 30 seconds
  baseReconnectDelay = 1000; // Start with 1 second delay
  reconnectTimeout = null;
  isConnecting = false;
  lastMessageAt = 0;

  constructor() {
    this.connect();

    setInterval(() => this._checkLiveness(), LIVENESS_CHECK_INTERVAL_MS);
    // A phone waking from sleep or regaining network shouldn't wait out the
    // exponential backoff before the dashboard comes back.
    window.addEventListener('visibilitychange', () => {
      if (!document.hidden) this._kickReconnect();
    });
    window.addEventListener('online', () => this._kickReconnect());
  }

  async connect() {
    if (this.isConnecting) return;
    this.isConnecting = true;

    try {
      if (this.socket) {
        // Replace before closing so the old socket's late close/error events
        // fail the `event.target !== this.socket` guard in the handlers and
        // can't tear down the new connection.
        const oldSocket = this.socket;
        this.socket = null;
        try {
          oldSocket.close();
        } catch {
          // Old socket may already be dead; nothing to do.
        }
      }

      const apiHost = window.location.host;
      const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
      this.socket = new WebSocket(`${wsProtocol}${apiHost}/ws`);
      this.lastMessageAt = Date.now();

      this.socket.addEventListener('message', this._onMessage.bind(this));
      this.socket.addEventListener('close', this._onClose.bind(this));
      this.socket.addEventListener('error', this._onError.bind(this));
      this.socket.addEventListener('open', this._onOpen.bind(this));
    } catch (error) {
      console.error('WebSocket connection error:', error);
      this._scheduleReconnect();
    } finally {
      this.isConnecting = false;
    }
  }

  _checkLiveness() {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return;
    if (Date.now() - this.lastMessageAt > LIVENESS_TIMEOUT_MS) {
      console.warn('WebSocket silent for too long, forcing reconnect');
      this.socket.close();
    }
  }

  _kickReconnect() {
    if (this.socket && this.socket.readyState === WebSocket.OPEN) {
      this._checkLiveness();
      return;
    }
    if (this.isConnecting || (this.socket && this.socket.readyState === WebSocket.CONNECTING)) {
      return;
    }
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    this.reconnectAttempts = 0;
    this.connect();
  }

  _onOpen(event) {
    if (event.target !== this.socket) return;
    console.log('WebSocket connected successfully');
    this.reconnectAttempts = 0;
    this.lastMessageAt = Date.now();
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    machine.value = {
      ...machine.value,
      connected: true,
    };
  }

  _onClose(event) {
    if (event.target !== this.socket) return;
    console.log('WebSocket connection closed');
    machine.value = {
      ...machine.value,
      connected: false,
    };
    // Fail in-flight requests now instead of letting them run out their full
    // timeout, so pages can surface the disconnect immediately.
    const pending = [...this.pendingRequests.values()];
    this.pendingRequests.clear();
    for (const fail of pending) {
      fail(new Error('WebSocket connection lost'));
    }
    this._scheduleReconnect();
  }

  _onError(error) {
    if (error.target !== this.socket) return;
    console.error('WebSocket error:', error);
    if (this.socket) {
      this.socket.close();
    }
  }

  _scheduleReconnect() {
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
    }

    // Calculate delay with exponential backoff
    const delay = Math.min(
      this.baseReconnectDelay * Math.pow(2, this.reconnectAttempts),
      this.maxReconnectDelay,
    );

    console.log(`Scheduling reconnect attempt ${this.reconnectAttempts + 1} in ${delay}ms`);

    this.reconnectTimeout = setTimeout(() => {
      this.reconnectAttempts++;
      this.connect();
    }, delay);
  }

  _onMessage(event) {
    if (event.target !== this.socket) return;
    this.lastMessageAt = Date.now();
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return; // Discard malformed messages to avoid crashing the WS handler.
    }
    const listeners = Object.values(this.listeners[message.tp] || {});
    if (message.tp === 'evt:status') {
      this._onStatus(message);
    }
    for (const listener of listeners) {
      listener(message);
    }
  }

  send(event) {
    if (this.socket && this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(event));
    } else {
      throw new Error('WebSocket is not connected');
    }
  }

  async request(data = {}) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket is not connected');
    }

    const returnType = `res:${data.tp.substring(4)}`;
    const rid = uuidv4();
    const message = { ...data, rid };
    return new Promise((resolve, reject) => {
      let timeoutId;

      const cleanup = () => {
        clearTimeout(timeoutId);
        this.off(returnType, listenerId);
        this.pendingRequests.delete(rid);
      };

      // Create a listener for the response with matching rid
      const listenerId = this.on(returnType, response => {
        if (response.rid === rid) {
          cleanup();
          resolve(response);
        }
      });

      // Registered so _onClose can fail this request the moment the socket
      // drops instead of waiting out the timeout below.
      this.pendingRequests.set(rid, error => {
        cleanup();
        reject(error);
      });

      try {
        this.send(message);
      } catch (error) {
        cleanup();
        reject(error);
        return;
      }

      // Timeout: reject if no matching response arrives within 30 seconds
      timeoutId = setTimeout(() => {
        cleanup();
        reject(new Error(`Request ${data.tp} timed out`));
      }, 30000); // 30 second timeout
    });
  }

  on(type, listener) {
    // Monotonic ids: the previous Math.random-derived ids could collide (or
    // come out empty), silently dropping another caller's listener.
    const id = `l${++this.listenerIdCounter}`;
    if (!this.listeners[type]) {
      this.listeners[type] = {};
    }
    this.listeners[type][id] = listener;
    return id;
  }

  off(type, id) {
    if (this.listeners[type]) {
      delete this.listeners[type][id];
    }
  }

  _onStatus(message) {
    const newStatus = {
      currentTemperature: message.ct,
      targetTemperature: message.tt,
      currentPressure: message.pr,
      targetPressure: message.pt,
      targetWeight: message.tw || 0,
      activeTargetWeight: (message?.process?.a && message.tw) || 0,
      currentFlow: message.fl,
      targetFlow: message.tf || 0,
      mode: message.m,
      selectedProfile: message.p,
      selectedProfileId: message.puid,
      brewTarget: !!message.bt,
      brewTargetDuration: message.btd || 0,
      volumetricAvailable: message.bta || false,
      grindTargetDuration: message.gtd || 0,
      grindTargetVolume: message.gtv || 0,
      grindTarget: message.gt || 0,
      grindActive: message.gact || false,
      currentWeight: message.cw || 0,
      bluetoothConnected: message.bc || false,
      process: message.process || null,
      timestamp: new Date(),
      rssi: message.rssi || 0,
      lat: message.lat || 0,
      tofDistance: message.tof || 0,
      currentPumpPower: message.pw ?? 0,
      currentBoilerPower: message.hp ?? 0,
      currentPuckResistance: message.pkr ?? 0,
      currentPuckFlow: message.pf ?? 0,
      currentCoffeeVolume: message.cv ?? 0,
      update: !!message.up,
    };
    const historyEntry = { ...newStatus };
    delete historyEntry.process;

    // Single allocation per tick: drop the oldest entry once the buffer is
    // full instead of copying the 600-entry array twice, 2×/second, forever.
    const prevHistory = machine.value.history;
    const history = prevHistory.slice(prevHistory.length >= 600 ? 1 : 0);
    history.push(historyEntry);

    // Capabilities almost never change — keep the object identity stable so
    // subscribers deriving from it (computed signals, memo deps) don't see a
    // fresh object every 500 ms.
    const prevCapabilities = machine.value.capabilities;
    const mergedCapabilities = {
      ...prevCapabilities,
      dimming: message.cd,
      pressure: message.cp,
      ledControl: message.led,
      gearpumpAddon: !!message.gp,
    };
    const capabilitiesChanged = Object.keys(mergedCapabilities).some(
      key => mergedCapabilities[key] !== prevCapabilities[key],
    );

    machine.value = {
      ...machine.value,
      connected: true,
      status: {
        ...machine.value.status,
        ...newStatus,
      },
      capabilities: capabilitiesChanged ? mergedCapabilities : prevCapabilities,
      history,
    };
  }
}

export const ApiServiceContext = createContext(null);

export const machine = signal({
  connected: false,
  status: {
    currentTemperature: 0,
    targetTemperature: 0,
    currentFlow: 0,
    targetFlow: 0,
    mode: 0,
    selectedProfile: '',
    selectedProfileId: null,
    brewTargetDuration: 0,
    brewTargetVolume: 0,
    grindTargetDuration: 0,
    grindTargetVolume: 0,
    grindTarget: 0,
    grindActive: false,
    process: null,
    update: false,
  },
  capabilities: {
    pressure: false,
    dimming: false,
  },
  history: [],
});

let settingsCache = null;
let settingsData = null;

export const prefetchSettings = () => {
  if (!settingsCache) {
    // Abort on stall so the cache doesn't hold a forever-pending promise —
    // the catch below only self-heals on rejection.
    const signal =
      typeof AbortSignal.timeout === 'function' ? AbortSignal.timeout(15000) : undefined;
    settingsCache = fetch('/api/settings', { signal })
      .then(res => {
        if (!res.ok) {
          throw new Error(`HTTP error! status: ${res.status}`);
        }
        return res.json();
      })
      .then(data => {
        settingsData = data;
        return data;
      })
      .catch(err => {
        settingsCache = null;
        throw err;
      });
  }
  return settingsCache;
};

export const getCachedSettings = () => settingsData;

export const updateSettingsCache = data => {
  settingsData = data;
  settingsCache = Promise.resolve(data);
};

export const invalidateSettingsCache = () => {
  settingsData = null;
  settingsCache = null;
};
