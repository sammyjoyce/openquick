// src/index.ts
var jsonHeaders = {
  accept: "application/json",
  "content-type": "application/json"
};
var identityCache = null;
var identityRequest = null;
var identityListeners = new Set;
function apiPath(...parts) {
  return `/_quick/${parts.map((part) => encodeURIComponent(part)).join("/")}`;
}
function requireId(id, label) {
  if (typeof id !== "string" || id.length === 0) {
    throw new TypeError(`${label} must be a non-empty string`);
  }
  return id;
}
function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}
async function parseResponse(response) {
  if (response.status === 204) {
    return;
  }
  const contentType = response.headers.get("content-type") || "";
  if (contentType.includes("application/json")) {
    return await response.json();
  }
  return await response.text();
}
async function requestJson(path, init = {}) {
  const headers = new Headers(init.headers);
  headers.set("accept", "application/json");
  const isFormData = typeof FormData !== "undefined" && init.body instanceof FormData;
  if (init.body !== undefined && !headers.has("content-type") && !isFormData) {
    headers.set("content-type", "application/json");
  }
  const response = await fetch(path, {
    ...init,
    headers,
    credentials: "same-origin"
  });
  if (!response.ok) {
    let details = response.statusText;
    try {
      const body = await parseResponse(response);
      if (typeof body === "string" && body.length > 0) {
        details = body;
      } else if (body && typeof body === "object" && "error" in body) {
        details = String(body.error);
      }
    } catch {}
    throw new Error(`OpenQuick request failed: ${response.status} ${details}`);
  }
  return parseResponse(response);
}
function normalizeList(value) {
  if (Array.isArray(value)) {
    return value;
  }
  if (value && typeof value === "object" && Array.isArray(value.items)) {
    return value.items;
  }
  return [];
}
function notifyIdentity(identity) {
  for (const listener of Array.from(identityListeners)) {
    listener(identity);
  }
}
async function currentIdentity() {
  if (identityRequest) {
    return identityRequest;
  }
  identityRequest = requestJson(apiPath("identity")).then((identity) => {
    identityCache = identity;
    notifyIdentity(identity);
    return identity;
  });
  return identityRequest;
}
function onIdentityChange(cb) {
  identityListeners.add(cb);
  if (identityCache) {
    queueMicrotask(() => {
      if (identityListeners.has(cb) && identityCache) {
        cb(identityCache);
      }
    });
  } else {
    currentIdentity().catch(() => {});
  }
  return () => {
    identityListeners.delete(cb);
  };
}

class RealtimeClient {
  socket = null;
  openPromise = null;
  queued = [];
  channelSince = new Map;
  listeners = new Map;
  on(channel, event, cb, since) {
    requireId(channel, "channel");
    requireId(event, "event");
    let events = this.listeners.get(channel);
    if (!events) {
      events = new Map;
      this.listeners.set(channel, events);
    }
    let callbacks = events.get(event);
    if (!callbacks) {
      callbacks = new Set;
      events.set(event, callbacks);
    }
    callbacks.add(cb);
    this.subscribe(channel, since);
    return () => {
      callbacks?.delete(cb);
      if (callbacks?.size === 0) {
        events?.delete(event);
      }
      if (events?.size === 0) {
        this.listeners.delete(channel);
      }
    };
  }
  subscribe(channel, since) {
    requireId(channel, "channel");
    if (!this.channelSince.has(channel)) {
      this.channelSince.set(channel, since);
      const socket = this.ensureSocket();
      if (socket.readyState === WebSocket.OPEN) {
        this.sendNow({ type: "subscribe", channel, since });
      }
      return;
    }
    if (since && !this.channelSince.get(channel)) {
      this.channelSince.set(channel, since);
    }
  }
  publish(channel, event, data) {
    requireId(channel, "channel");
    requireId(event, "event");
    this.sendEnvelope({ type: "publish", channel, event, data });
  }
  async open() {
    const socket = this.ensureSocket();
    if (socket.readyState === WebSocket.OPEN) {
      return;
    }
    if (!this.openPromise) {
      this.openPromise = new Promise((resolve, reject) => {
        const onOpen = () => {
          cleanup();
          resolve();
        };
        const onError = () => {
          cleanup();
          reject(new Error("OpenQuick realtime connection failed"));
        };
        const cleanup = () => {
          socket.removeEventListener("open", onOpen);
          socket.removeEventListener("error", onError);
        };
        socket.addEventListener("open", onOpen);
        socket.addEventListener("error", onError);
      });
    }
    return this.openPromise;
  }
  ensureSocket() {
    if (typeof WebSocket === "undefined") {
      throw new Error("OpenQuick realtime requires WebSocket support");
    }
    if (this.socket && this.socket.readyState !== WebSocket.CLOSED) {
      return this.socket;
    }
    const socket = new WebSocket("/_quick/realtime");
    this.socket = socket;
    this.openPromise = null;
    socket.addEventListener("open", () => {
      for (const [channel, since] of this.channelSince) {
        this.sendNow({ type: "subscribe", channel, since });
      }
      while (this.queued.length > 0 && socket.readyState === WebSocket.OPEN) {
        this.sendNow(this.queued.shift());
      }
    });
    socket.addEventListener("message", (event) => {
      this.handleMessage(event.data);
    });
    socket.addEventListener("close", () => {
      if (this.socket === socket) {
        this.socket = null;
        this.openPromise = null;
      }
    });
    return socket;
  }
  sendEnvelope(envelope) {
    const socket = this.ensureSocket();
    if (socket.readyState === WebSocket.OPEN) {
      this.sendNow(envelope);
      return;
    }
    this.queued.push(envelope);
  }
  sendNow(envelope) {
    this.socket?.send(JSON.stringify(envelope));
  }
  handleMessage(raw) {
    let envelope;
    try {
      envelope = JSON.parse(String(raw));
    } catch {
      return;
    }
    if (!envelope.channel || !envelope.event) {
      return;
    }
    const events = this.listeners.get(envelope.channel);
    if (!events) {
      return;
    }
    const callbacks = new Set([
      ...events.get(envelope.event) || [],
      ...events.get("*") || []
    ]);
    for (const callback of callbacks) {
      callback(envelope.data, envelope);
    }
  }
}
var realtimeClient = new RealtimeClient;
function collection(name) {
  requireId(name, "collection name");
  const base = apiPath("db", name);
  const channel = `db:${name}`;
  return {
    create(data) {
      return requestJson(base, {
        method: "POST",
        headers: jsonHeaders,
        body: JSON.stringify(data)
      });
    },
    get(id) {
      return requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`);
    },
    update(id, patch) {
      return requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "PATCH",
        headers: jsonHeaders,
        body: JSON.stringify(patch)
      });
    },
    async remove(id) {
      await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "DELETE"
      });
    },
    async list() {
      return normalizeList(await requestJson(base));
    },
    async subscribe(handlers) {
      const unsubs = [];
      if (handlers.onCreate) {
        unsubs.push(realtimeClient.on(channel, "create", (data) => handlers.onCreate?.(data), handlers.since));
      }
      if (handlers.onUpdate) {
        unsubs.push(realtimeClient.on(channel, "update", (data) => handlers.onUpdate?.(data), handlers.since));
      }
      if (handlers.onDelete) {
        unsubs.push(realtimeClient.on(channel, "delete", (data) => handlers.onDelete?.(typeof data === "string" ? data : String(data?.id || "")), handlers.since));
      }
      realtimeClient.subscribe(channel, handlers.since);
      await realtimeClient.open();
      return () => {
        for (const unsub of unsubs.splice(0)) {
          unsub();
        }
      };
    }
  };
}
async function putUpload(file, options = {}) {
  if (typeof FormData === "undefined") {
    throw new Error("OpenQuick uploads require FormData support");
  }
  const name = options.name || ("name" in file && typeof file.name === "string" ? file.name : "upload");
  const form = new FormData;
  form.append("file", file, name);
  form.append("name", name);
  return requestJson(apiPath("uploads"), {
    method: "POST",
    body: form
  });
}
function getUpload(id) {
  return requestJson(apiPath("uploads", requireId(id, "upload id")));
}
async function removeUpload(id) {
  await requestJson(apiPath("uploads", requireId(id, "upload id")), {
    method: "DELETE"
  });
}
function realtimeChannel(name) {
  requireId(name, "channel name");
  return {
    on(event, cb) {
      return realtimeClient.on(name, event, cb);
    },
    send(event, data) {
      realtimeClient.publish(name, event, data);
    }
  };
}
function fetchCapabilities() {
  return requestJson(apiPath("capabilities"));
}
async function chat(messages, options = {}) {
  try {
    await fetchCapabilities();
  } catch (error) {
    throw new Error(`quick.ai.chat is not available on this host (capabilities check failed: ${errorMessage(error)})`);
  }
  throw new Error("quick.ai.chat is not available on this host");
}
var quick = {
  identity: {
    current: currentIdentity,
    onChange: onIdentityChange
  },
  db: {
    collection
  },
  uploads: {
    put: putUpload,
    get: getUpload,
    remove: removeUpload
  },
  realtime: {
    channel: realtimeChannel
  },
  ai: {
    chat
  },
  capabilities: fetchCapabilities
};
var src_default = quick;
export {
  quick,
  src_default as default
};
