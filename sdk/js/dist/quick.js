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

class OpenQuickError extends Error {
  status;
  code;
  details;
  retryAfter;
  constructor(message, options) {
    super(message, options.cause !== undefined ? { cause: options.cause } : undefined);
    this.name = "OpenQuickError";
    this.status = options.status;
    this.code = options.code;
    this.details = options.details;
    this.retryAfter = options.retryAfter;
  }
}
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
  let code;
  try {
    const body = await parseResponse(response);
    if (typeof body === "string" && body.length > 0) {
      details = body;
    } else if (isRecord(body)) {
      details = body;
      if (typeof body.code === "string") {
        code = body.code;
      } else if (typeof body.error === "string") {
        code = body.error;
      }
    }
  } catch {}
  const retryAfterHeader = response.headers.get("retry-after");
  const retryAfter = retryAfterHeader ? Number.parseInt(retryAfterHeader, 10) : undefined;
  const messageDetail = isRecord(details) && typeof details.error === "string" ? details.error : String(details);
  throw new OpenQuickError(`OpenQuick request failed: ${response.status} ${messageDetail}`, {
    status: response.status,
    code,
    details,
    retryAfter: Number.isFinite(retryAfter) ? retryAfter : undefined
  });
}
function isAbortError(err) {
  return Boolean(err && typeof err === "object" && (err.name === "AbortError" || err.code === "ABORT_ERR"));
}
function throwAbortError(err) {
  throw new OpenQuickError("OpenQuick request aborted", {
    code: "aborted",
    details: { error: "aborted", cause: err instanceof Error ? err.message : String(err) }
  });
}
async function requestJson(path, init = {}) {
  const headers = new Headers(init.headers);
  headers.set("accept", "application/json");
  const isFormData = typeof FormData !== "undefined" && init.body instanceof FormData;
  if (init.body !== undefined && !headers.has("content-type") && !isFormData) {
    headers.set("content-type", "application/json");
  }
  let response;
  try {
    response = await fetch(path, {
      ...init,
      headers,
      credentials: "same-origin"
    });
  } catch (err) {
    if (isAbortError(err))
      throwAbortError(err);
    throw err;
  }
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
function normalizeListResult(value) {
  const docs = normalizeList(value).map((doc) => normalizeDocument(doc));
  docs.documents = docs;
  if (value && typeof value === "object") {
    const next = value.next_cursor ?? value.nextCursor;
    if (typeof next === "string" && next.length > 0) {
      docs.next_cursor = next;
      docs.nextCursor = next;
    }
  }
  return docs;
}
function dbListPath(base, options) {
  const params = new URLSearchParams;
  if (options.limit !== undefined) {
    params.set("limit", String(options.limit));
  }
  if (options.cursor) {
    params.set("cursor", options.cursor);
  }
  if (options.filter !== undefined) {
    params.set("filter", typeof options.filter === "string" ? options.filter : JSON.stringify(options.filter));
  }
  if (options.sort !== undefined) {
    params.set("sort", Array.isArray(options.sort) ? options.sort.join(",") : options.sort);
  }
  const qs = params.toString();
  return qs ? `${base}?${qs}` : base;
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
  reconnectTimer = null;
  reconnectAttempt = 0;
  state = "disconnected";
  queued = [];
  channelSince = new Map;
  listeners = new Map;
  stateListeners = new Set;
  onStateChange(cb) {
    this.stateListeners.add(cb);
    queueMicrotask(() => {
      if (this.stateListeners.has(cb)) {
        cb(this.state);
      }
    });
    return () => {
      this.stateListeners.delete(cb);
    };
  }
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
        this.channelSince.delete(channel);
      }
      if (!this.hasActiveWork()) {
        this.clearReconnectTimer();
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
    this.setState(this.state === "reconnecting" ? "reconnecting" : "connecting");
    socket.addEventListener("open", () => {
      this.reconnectAttempt = 0;
      this.clearReconnectTimer();
      this.setState("connected");
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
        this.setState("disconnected");
        if (this.hasActiveWork()) {
          this.scheduleReconnect();
        }
      }
    });
    return socket;
  }
  hasActiveWork() {
    return this.listeners.size > 0 || this.queued.length > 0 || this.channelSince.size > 0;
  }
  setState(state) {
    if (this.state === state) {
      return;
    }
    this.state = state;
    for (const listener of Array.from(this.stateListeners)) {
      listener(state);
    }
  }
  clearReconnectTimer() {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }
  scheduleReconnect() {
    if (this.reconnectTimer !== null) {
      return;
    }
    this.setState("reconnecting");
    const delay = Math.min(30000, 1000 * 2 ** Math.min(this.reconnectAttempt, 5));
    this.reconnectAttempt += 1;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.hasActiveWork()) {
        this.setState("disconnected");
        return;
      }
      try {
        this.ensureSocket();
      } catch {
        this.scheduleReconnect();
      }
    }, delay);
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
    async create(data, options = {}) {
      return normalizeDocument(await requestJson(base, {
        method: "POST",
        headers: jsonHeaders,
        body: JSON.stringify(data),
        signal: options.signal
      }));
    },
    async get(id, options = {}) {
      return normalizeDocument(await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, { signal: options.signal }));
    },
    async update(id, patch, options = {}) {
      return normalizeDocument(await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "PATCH",
        headers: jsonHeaders,
        body: JSON.stringify(patch),
        signal: options.signal
      }));
    },
    async remove(id, options = {}) {
      await requestJson(`${base}/${encodeURIComponent(requireId(id, "document id"))}`, {
        method: "DELETE",
        signal: options.signal
      });
    },
    async list(options = {}) {
      const raw = await requestJson(dbListPath(base, options), { signal: options.signal });
      return normalizeListResult(raw);
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
function progressBody(file, onProgress) {
  if (typeof ReadableStream === "undefined" || typeof file.stream !== "function") {
    return null;
  }
  const total = typeof file.size === "number" ? file.size : undefined;
  let loaded = 0;
  const reader = file.stream().getReader();
  return new ReadableStream({
    async pull(controller) {
      const chunk = await reader.read();
      if (chunk.done) {
        controller.close();
        return;
      }
      loaded += chunk.value.byteLength || chunk.value.length || 0;
      onProgress({ loaded, total, percent: total ? loaded / total : undefined });
      controller.enqueue(chunk.value);
    },
    cancel(reason) {
      return reader.cancel(reason);
    }
  });
}
async function putUpload(file, options = {}) {
  const name = options.name || ("name" in file && typeof file.name === "string" ? file.name : "upload");
  if (options.onProgress) {
    const body = progressBody(file, options.onProgress);
    if (body) {
      const upload2 = await requestJson(`${apiPath("uploads")}?name=${encodeURIComponent(name)}`, {
        method: "POST",
        headers: { "content-type": file.type || "application/octet-stream" },
        body,
        signal: options.signal,
        ...typeof navigator === "undefined" ? { duplex: "half" } : {}
      });
      return pathAwareURL(upload2);
    }
  }
  if (typeof FormData === "undefined") {
    throw new Error("OpenQuick uploads require FormData support");
  }
  const form = new FormData;
  form.append("file", file, name);
  form.append("name", name);
  const upload = await requestJson(apiPath("uploads"), {
    method: "POST",
    body: form,
    signal: options.signal
  });
  return pathAwareURL(upload);
}
async function getUpload(id, options = {}) {
  const upload = await requestJson(apiPath("uploads", requireId(id, "upload id")), { signal: options.signal });
  return pathAwareURL(upload);
}
async function removeUpload(id, options = {}) {
  await requestJson(apiPath("uploads", requireId(id, "upload id")), {
    method: "DELETE",
    signal: options.signal
  });
}
async function listUploads(options = {}) {
  const params = new URLSearchParams;
  if (options.limit !== undefined)
    params.set("limit", String(options.limit));
  if (options.cursor)
    params.set("cursor", options.cursor);
  const url = apiPath("uploads") + (params.toString() ? `?${params}` : "");
  const envelope = await requestJson(url, {
    signal: options.signal
  });
  const uploads = (envelope.uploads || []).map(pathAwareURL);
  uploads.uploads = uploads;
  uploads.next_cursor = envelope.next_cursor || undefined;
  uploads.nextCursor = uploads.next_cursor;
  return uploads;
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
function fetchCapabilities(options = {}) {
  return requestJson(apiPath("capabilities"), { signal: options.signal });
}
async function requireCapability(apiName, capability, options = {}) {
  let capabilities;
  try {
    capabilities = await fetchCapabilities(options);
  } catch (error) {
    if (error instanceof OpenQuickError && error.code === "aborted") {
      throw error;
    }
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
async function requestChatStream(body, fallbackModel, onDelta, signal) {
  let response;
  try {
    response = await fetch(apiPath("ai", "chat"), {
      method: "POST",
      headers: {
        accept: "text/event-stream, application/json",
        "content-type": "application/json"
      },
      body: JSON.stringify(body),
      credentials: "same-origin",
      signal
    });
  } catch (err) {
    if (isAbortError(err))
      throwAbortError(err);
    throw err;
  }
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
  await requireCapability("quick.ai.chat", "ai", { signal: options.signal });
  const { model, stream, signal, onDelta, ...rest } = options;
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
    return requestChatStream(body, model, onDelta, signal);
  }
  return requestJson(apiPath("ai", "chat"), {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify(body),
    signal
  });
}
async function image(prompt, options = {}) {
  requireId(prompt, "prompt");
  await requireCapability("quick.ai.image", "ai", { signal: options.signal });
  const { model, size, signal, ...rest } = options;
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
    body: JSON.stringify(body),
    signal
  });
  return pathAwareURL(image2);
}
async function warehouseMetadata(options = {}) {
  await requireCapability("quick.warehouse.metadata", "warehouse", options);
  return requestJson(apiPath("warehouse"), { signal: options.signal });
}
async function warehouseQuery(name, params = {}, options = {}) {
  requireId(name, "warehouse query name");
  await requireCapability("quick.warehouse.query", "warehouse", options);
  return requestJson(apiPath("warehouse", name), {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify(params),
    signal: options.signal
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
    list: listUploads,
    remove: removeUpload
  },
  realtime: {
    channel: realtimeChannel,
    onStateChange: (cb) => realtimeClient.onStateChange(cb)
  },
  ai: {
    chat,
    image
  },
  warehouse: {
    metadata: warehouseMetadata,
    query: warehouseQuery
  },
  capabilities: fetchCapabilities
};
var src_default = quick;
export {
  quick,
  src_default as default,
  OpenQuickError
};
