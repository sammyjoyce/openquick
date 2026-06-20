// src/index.ts
var jsonHeaders = {
  accept: "application/json",
  "content-type": "application/json"
};
var identityCache = null;
var identityRequest = null;
var identityListeners = new Set;
var rootQuickBase = "/_quick";
function locationPathFallbackBase() {
  if (typeof location === "undefined" || typeof location.pathname !== "string") {
    return null;
  }
  const parts = location.pathname.split("/");
  if (parts.length >= 3 && parts[1] === "~" && parts[2]) {
    return `/~/${parts[2]}/_quick`;
  }
  return null;
}
function sdkPathBase() {
  try {
    const sdkURL = new URL(import.meta.url);
    if (!sdkURL.pathname.endsWith("/_quick/sdk.js")) {
      return null;
    }
    const base = sdkURL.pathname.slice(0, -"/sdk.js".length);
    if (base === rootQuickBase) {
      return locationPathFallbackBase() || rootQuickBase;
    }
    return base;
  } catch {
    return null;
  }
}
function detectApiBase() {
  return sdkPathBase() || locationPathFallbackBase() || rootQuickBase;
}
var apiBase = detectApiBase();
function apiPath(...parts) {
  const suffix = parts.map((part) => encodeURIComponent(part)).join("/");
  return suffix ? `${apiBase}/${suffix}` : apiBase;
}
function realtimeURL(...parts) {
  const pageURL = typeof location === "undefined" ? "http://localhost/" : location.href;
  const url = new URL(apiPath(...parts), pageURL);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url.href;
}
function pathAwareURL(value) {
  if (!value || typeof value !== "object" || typeof value.url !== "string" || !value.url.startsWith(`${rootQuickBase}/`)) {
    return value;
  }
  return { ...value, url: `${apiBase}${value.url.slice(rootQuickBase.length)}` };
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
async function throwRequestError(response) {
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
    await throwRequestError(response);
  }
  return parseResponse(response);
}
function normalizeList(value) {
  if (Array.isArray(value)) {
    return value;
  }
  if (value && typeof value === "object") {
    if (Array.isArray(value.documents)) {
      return value.documents;
    }
    if (Array.isArray(value.items)) {
      return value.items;
    }
  }
  return [];
}
function normalizeDocument(value) {
  if (value && typeof value === "object" && "data" in value) {
    const { data, ...meta } = value;
    if (data && typeof data === "object" && !Array.isArray(data)) {
      const result = { ...meta, ...data };
      if (Object.prototype.hasOwnProperty.call(value, "id")) {
        result.id = value.id;
      }
      return result;
    }
  }
  return value;
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
    const socket = new WebSocket(realtimeURL("realtime"));
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
    async create(data) {
      return normalizeDocument(await requestJson(base, {
        method: "POST",
        headers: jsonHeaders,
        body: JSON.stringify(data)
      }));
    },
    async get(id) {
      return normalizeDocument(await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`));
    },
    async update(id, patch) {
      return normalizeDocument(await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "PATCH",
        headers: jsonHeaders,
        body: JSON.stringify(patch)
      }));
    },
    async remove(id) {
      await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "DELETE"
      });
    },
    async list() {
      const raw = await requestJson(base);
      return normalizeList(raw).map((doc) => normalizeDocument(doc));
    },
    async subscribe(handlers) {
      const unsubs = [];
      if (handlers.onCreate) {
        unsubs.push(realtimeClient.on(channel, "create", (data) => handlers.onCreate?.(normalizeDocument(data)), handlers.since));
      }
      if (handlers.onUpdate) {
        unsubs.push(realtimeClient.on(channel, "update", (data) => handlers.onUpdate?.(normalizeDocument(data)), handlers.since));
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
  const upload = await requestJson(apiPath("uploads"), {
    method: "POST",
    body: form
  });
  return pathAwareURL(upload);
}
async function getUpload(id) {
  const upload = await requestJson(apiPath("uploads", requireId(id, "upload id")));
  return pathAwareURL(upload);
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
async function requireCapability(apiName, capability) {
  let capabilities;
  try {
    capabilities = await fetchCapabilities();
  } catch (error) {
    throw new Error(`${apiName} is not available on this host (capabilities check failed: ${errorMessage(error)})`);
  }
  if (capabilities[capability] !== true) {
    throw new Error(`${apiName} is not available on this host`);
  }
}
function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
function extractDeltaText(payload) {
  if (typeof payload === "string") {
    return payload;
  }
  if (!isRecord(payload)) {
    return "";
  }
  const delta = payload.delta;
  if (typeof delta === "string") {
    return delta;
  }
  if (isRecord(delta) && typeof delta.content === "string") {
    return delta.content;
  }
  return "";
}
function processChatStreamEvent(rawEvent, state, onDelta) {
  const data = rawEvent.split(/\r?\n/).filter((line) => line.startsWith("data:")).map((line) => {
    const value = line.slice(5);
    return value.startsWith(" ") ? value.slice(1) : value;
  }).join(`
`);
  if (data.trim().length === 0) {
    return false;
  }
  const trimmed = data.trim();
  if (trimmed === "[DONE]" || trimmed.toLowerCase() === "done") {
    return true;
  }
  let payload = data;
  try {
    payload = JSON.parse(data);
  } catch {}
  if (isRecord(payload)) {
    const keys = Object.keys(payload);
    if (!(keys.length === 1 && payload.done === true)) {
      state.lastPayload = payload;
    }
  }
  const delta = extractDeltaText(payload);
  if (delta.length > 0) {
    state.content += delta;
    onDelta?.(delta, payload);
  }
  return isRecord(payload) && payload.done === true;
}
function buildStreamChatResponse(state, fallbackModel) {
  const response = state.lastPayload ? { ...state.lastPayload } : {};
  const existingMessage = isRecord(response.message) ? response.message : {};
  const content = typeof existingMessage.content === "string" && existingMessage.content.length > 0 ? existingMessage.content : state.content;
  response.message = {
    role: typeof existingMessage.role === "string" ? existingMessage.role : "assistant",
    ...existingMessage,
    content
  };
  if (typeof response.model !== "string" && typeof fallbackModel === "string") {
    response.model = fallbackModel;
  }
  return response;
}
async function requestChatStream(body, fallbackModel, onDelta) {
  const response = await fetch(apiPath("ai", "chat"), {
    method: "POST",
    headers: {
      accept: "text/event-stream, application/json",
      "content-type": "application/json"
    },
    body: JSON.stringify(body),
    credentials: "same-origin"
  });
  if (!response.ok) {
    await throwRequestError(response);
  }
  const contentType = response.headers.get("content-type") || "";
  if (!response.body || !contentType.includes("text/event-stream")) {
    const parsed = await parseResponse(response);
    const content = isRecord(parsed.message) && typeof parsed.message.content === "string" ? parsed.message.content : "";
    if (content.length > 0) {
      onDelta?.(content, parsed);
    }
    return parsed;
  }
  const reader = response.body.getReader();
  const decoder = new TextDecoder;
  const state = { content: "", lastPayload: null };
  let buffer = "";
  let sawDone = false;
  while (!sawDone) {
    const chunk = await reader.read();
    if (chunk.done) {
      break;
    }
    buffer += decoder.decode(chunk.value, { stream: true });
    const events = buffer.split(/\r?\n\r?\n/);
    buffer = events.pop() || "";
    for (const event of events) {
      if (processChatStreamEvent(event, state, onDelta)) {
        sawDone = true;
        break;
      }
    }
  }
  buffer += decoder.decode();
  if (!sawDone && buffer.trim().length > 0) {
    sawDone = processChatStreamEvent(buffer, state, onDelta);
  }
  if (sawDone) {
    await reader.cancel().catch(() => {});
  }
  return buildStreamChatResponse(state, fallbackModel);
}
async function chat(messages, options = {}) {
  await requireCapability("quick.ai.chat", "ai");
  const { model, stream, onDelta, ...rest } = options;
  const useStream = stream === true || typeof onDelta === "function";
  const body = {
    ...rest,
    messages,
    stream: useStream
  };
  if (model !== undefined) {
    body.model = model;
  }
  if (useStream) {
    return requestChatStream(body, model, onDelta);
  }
  return requestJson(apiPath("ai", "chat"), {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify(body)
  });
}
async function image(prompt, options = {}) {
  requireId(prompt, "prompt");
  await requireCapability("quick.ai.image", "ai");
  const { model, size, ...rest } = options;
  const body = { ...rest, prompt };
  if (model !== undefined) {
    body.model = model;
  }
  if (size !== undefined) {
    body.size = size;
  }
  const image2 = await requestJson(apiPath("ai", "images"), {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify(body)
  });
  return pathAwareURL(image2);
}
async function warehouseQuery(name, params = {}) {
  requireId(name, "warehouse query name");
  await requireCapability("quick.warehouse.query", "warehouse");
  return requestJson(apiPath("warehouse", name), {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify(params)
  });
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
    chat,
    image
  },
  warehouse: {
    query: warehouseQuery
  },
  capabilities: fetchCapabilities
};
var src_default = quick;
export {
  quick,
  src_default as default
};
