export interface Identity {
  authenticated: boolean;
  provider: string;
  subject: string;
  email?: string;
  login?: string;
  name?: string;
  avatar_url?: string;
  groups?: string[];
  device?: string;
  capabilities?: Record<string, unknown>;
  [key: string]: unknown;
}

export interface Capabilities {
  [key: string]: unknown;
}

export interface DocumentRecord {
  id: string;
  [key: string]: unknown;
}

export interface DbSubscriptionHandlers<T = DocumentRecord> {
  since?: string;
  onCreate?: (doc: T) => void;
  onUpdate?: (doc: T) => void;
  onDelete?: (id: string) => void;
}

export interface Collection<T = DocumentRecord> {
  create(data: Record<string, unknown>, options?: RequestOptions): Promise<T>;
  get(id: string, options?: RequestOptions): Promise<T>;
  update(id: string, patch: Record<string, unknown>, options?: DbWriteOptions): Promise<T>;
  remove(id: string, options?: DbWriteOptions): Promise<void>;
  list(options?: DbListOptions): Promise<ListResult<T>>;
  subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void>;
}

export interface RequestOptions {
  signal?: AbortSignal;
}

export interface DbWriteOptions extends RequestOptions {
  revision?: string;
}

export interface DbListOptions extends RequestOptions {
  limit?: number;
  cursor?: string;
  filter?: Record<string, unknown> | string;
  sort?: string | string[];
}

export interface ListResult<T = DocumentRecord> extends Array<T> {
  documents: T[];
  next_cursor?: string;
  nextCursor?: string;
}

export interface UploadProgress {
  loaded: number;
  total?: number;
  percent?: number;
}

export interface UploadOptions extends RequestOptions {
  name?: string;
  onProgress?: (progress: UploadProgress) => void;
}

export interface UploadListOptions extends RequestOptions {
  limit?: number;
  cursor?: string;
}

export interface UploadListResult extends Array<Upload> {
  uploads: Upload[];
  next_cursor?: string;
  nextCursor?: string;
}

export interface Upload {
  id: string;
  url: string;
  name?: string;
  content_type?: string;
  size?: number;
  created_at?: string;
  [key: string]: unknown;
}

export interface RealtimeEnvelope {
  type: 'subscribe' | 'publish';
  channel: string;
  since?: string;
  event?: string;
  data?: unknown;
}

export interface Channel {
  on(event: string, cb: (data: unknown, envelope: RealtimeEnvelope) => void): () => void;
  send(event: string, data?: unknown): void;
}

export type RealtimeConnectionState = 'disconnected' | 'connecting' | 'connected' | 'reconnecting';

export type RealtimeStateCallback = (state: RealtimeConnectionState) => void;

export interface ChatMessage {
  role: 'system' | 'user' | 'assistant' | string;
  content: string;
  [key: string]: unknown;
}

export interface ChatOptions {
  model?: string;
  stream?: boolean;
  signal?: AbortSignal;
  onDelta?: (delta: string, event: unknown) => void;
  [key: string]: unknown;
}

export interface ChatUsage {
  prompt_tokens?: number;
  completion_tokens?: number;
  [key: string]: unknown;
}

export interface ChatResponse {
  id?: string;
  model?: string;
  message: ChatMessage;
  usage?: ChatUsage;
  [key: string]: unknown;
}

export interface ImageOptions {
  model?: string;
  size?: string;
  signal?: AbortSignal;
  [key: string]: unknown;
}

export interface ImageResponse {
  id: string;
  model?: string;
  url: string;
  [key: string]: unknown;
}

export type WarehouseParams = Record<string, unknown>;

export interface WarehouseQueryResult {
  name: string;
  columns: string[];
  rows: unknown[][];
  row_count: number;
  truncated?: boolean;
  [key: string]: unknown;
}

export interface WarehouseMetadataQuery {
  name: string;
  params: Array<{ name: string; type: string }>;
  max_rows: number;
  columns: string[];
}

export interface WarehouseMetadataResult {
  queries: WarehouseMetadataQuery[];
}

type RealtimeCallback = (data: unknown, envelope: RealtimeEnvelope) => void;

type RealtimeListenerMap = Map<string, Set<RealtimeCallback>>;

const jsonHeaders = {
  accept: 'application/json',
  'content-type': 'application/json',
};

let identityCache: Identity | null = null;
let identityRequest: Promise<Identity> | null = null;
const identityListeners = new Set<(identity: Identity) => void>();
const rootQuickBase = '/_quick';

function locationPathFallbackBase(): string | null {
  if (typeof location === 'undefined' || typeof location.pathname !== 'string') {
    return null;
  }

  const parts = location.pathname.split('/');
  if (parts.length >= 3 && parts[1] === '~' && parts[2]) {
    return `/~/${parts[2]}/_quick`;
  }
  return null;
}

function sdkPathBase(): string | null {
  try {
    const sdkURL = new URL(import.meta.url);
    if (!sdkURL.pathname.endsWith('/_quick/sdk.js')) {
      return null;
    }

    const base = sdkURL.pathname.slice(0, -'/sdk.js'.length);
    if (base === rootQuickBase) {
      return locationPathFallbackBase() || rootQuickBase;
    }
    return base;
  } catch {
    return null;
  }
}

function detectApiBase(): string {
  return sdkPathBase() || locationPathFallbackBase() || rootQuickBase;
}

const apiBase = detectApiBase();

export class OpenQuickError extends Error {
  readonly status: number;
  readonly code?: string;
  readonly details?: unknown;
  readonly retryAfter?: number;

  constructor(message: string, options: { status: number; code?: string; details?: unknown; retryAfter?: number; cause?: unknown }) {
    super(message);
    if (options.cause !== undefined) {
      (this as { cause?: unknown }).cause = options.cause;
    }
    this.name = 'OpenQuickError';
    this.status = options.status;
    this.code = options.code;
    this.details = options.details;
    this.retryAfter = options.retryAfter;
  }
}

function apiPath(...parts: string[]): string {
  const suffix = parts.map((part) => encodeURIComponent(part)).join('/');
  return suffix ? `${apiBase}/${suffix}` : apiBase;
}

function realtimeURL(...parts: string[]): string {
  const pageURL = typeof location === 'undefined' ? 'http://localhost/' : location.href;
  const url = new URL(apiPath(...parts), pageURL);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  return url.href;
}

function pathAwareURL<T extends { url?: string }>(value: T): T {
  if (!value || typeof value !== 'object' || typeof value.url !== 'string' || !value.url.startsWith(`${rootQuickBase}/`)) {
    return value;
  }
  return { ...value, url: `${apiBase}${value.url.slice(rootQuickBase.length)}` };
}

function requireId(id: string, label: string): string {
  if (typeof id !== 'string' || id.length === 0) {
    throw new TypeError(`${label} must be a non-empty string`);
  }
  return id;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

async function parseResponse<T>(response: Response): Promise<T> {
  if (response.status === 204) {
    return undefined as T;
  }

  const contentType = response.headers.get('content-type') || '';
  if (contentType.includes('application/json')) {
    return (await response.json()) as T;
  }

  return (await response.text()) as T;
}

async function throwRequestError(response: Response): Promise<never> {
  let details: unknown = response.statusText;
  let code: string | undefined;
  try {
    const body = await parseResponse<unknown>(response);
    if (typeof body === 'string' && body.length > 0) {
      details = body;
    } else if (isRecord(body)) {
      details = body;
      if (typeof body.code === 'string') {
        code = body.code;
      } else if (typeof body.error === 'string') {
        code = body.error;
      }
    }
  } catch {
    // Keep the HTTP status text when the error body cannot be parsed.
  }

  const retryAfterHeader = response.headers.get('retry-after');
  const retryAfter = retryAfterHeader ? Number.parseInt(retryAfterHeader, 10) : undefined;
  const messageDetail = isRecord(details) && typeof details.error === 'string' ? details.error : String(details);
  throw new OpenQuickError(`OpenQuick request failed: ${response.status} ${messageDetail}`, {
    status: response.status,
    code,
    details,
    retryAfter: Number.isFinite(retryAfter) ? retryAfter : undefined,
  });
}

function isAbortError(err: unknown): boolean {
  return Boolean(err && typeof err === 'object' && ((err as { name?: string }).name === 'AbortError' || (err as { code?: string }).code === 'ABORT_ERR'));
}

function throwAbortError(err: unknown): never {
  throw new OpenQuickError('OpenQuick request aborted', {
    status: 0,
    code: 'aborted',
    details: { error: 'aborted', cause: err instanceof Error ? err.message : String(err) },
  });
}

async function requestJson<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  headers.set('accept', 'application/json');

  const isFormData = typeof FormData !== 'undefined' && init.body instanceof FormData;
  if (init.body !== undefined && !headers.has('content-type') && !isFormData) {
    headers.set('content-type', 'application/json');
  }

  let response: Response;
  try {
    response = await fetch(path, {
      ...init,
      headers,
      credentials: 'same-origin',
    });
  } catch (err) {
    if (isAbortError(err)) throwAbortError(err);
    throw err;
  }

  if (!response.ok) {
    await throwRequestError(response);
  }

  return parseResponse<T>(response);
}

function normalizeList<T>(value: T[] | { items?: T[]; documents?: T[] }): T[] {
  if (Array.isArray(value)) {
    return value;
  }
  if (value && typeof value === 'object') {
    if (Array.isArray(value.documents)) {
      return value.documents;
    }
    if (Array.isArray(value.items)) {
      return value.items;
    }
  }
  return [];
}

function normalizeListResult<T>(value: unknown): ListResult<T> {
  const docs = normalizeList<unknown>(value as unknown[] | { items?: unknown[]; documents?: unknown[] }).map((doc) => normalizeDocument<T>(doc)) as ListResult<T>;
  docs.documents = [...docs];
  if (value && typeof value === 'object') {
    const next = (value as { next_cursor?: unknown; nextCursor?: unknown }).next_cursor ?? (value as { nextCursor?: unknown }).nextCursor;
    if (typeof next === 'string' && next.length > 0) {
      docs.next_cursor = next;
      docs.nextCursor = next;
    }
  }
  return docs;
}

function dbListPath(base: string, options: DbListOptions): string {
  const params = new URLSearchParams();
  if (options.limit !== undefined) {
    params.set('limit', String(options.limit));
  }
  if (options.cursor) {
    params.set('cursor', options.cursor);
  }
  if (options.filter !== undefined) {
    params.set('filter', typeof options.filter === 'string' ? options.filter : JSON.stringify(options.filter));
  }
  if (options.sort !== undefined) {
    params.set('sort', Array.isArray(options.sort) ? options.sort.join(',') : options.sort);
  }
  const qs = params.toString();
  return qs ? `${base}?${qs}` : base;
}

function dbWriteHeaders(options: DbWriteOptions, baseHeaders: HeadersInit = {}): Headers {
  const headers = new Headers(baseHeaders);
  if (options.revision !== undefined) {
    headers.set('If-Match', options.revision);
  }
  return headers;
}

// quickd wraps document fields in a `data` envelope alongside server
// metadata. Flatten to the documented DocumentRecord shape: `id` plus the
// caller's fields, while keeping server metadata authoritative over any
// same-named user fields in `data`.
function normalizeDocument<T>(value: unknown): T {
  if (value && typeof value === 'object' && 'data' in value) {
    const { data, ...meta } = value as { data?: unknown; id?: unknown };
    if (data && typeof data === 'object' && !Array.isArray(data)) {
      return { ...(data as Record<string, unknown>), ...meta } as T;
    }
  }
  return value as T;
}

function notifyIdentity(identity: Identity): void {
  for (const listener of Array.from(identityListeners)) {
    listener(identity);
  }
}

async function currentIdentity(): Promise<Identity> {
  if (identityRequest) {
    return identityRequest;
  }

  identityRequest = requestJson<Identity>(apiPath('identity')).then((identity) => {
    identityCache = identity;
    notifyIdentity(identity);
    return identity;
  });

  return identityRequest;
}

function onIdentityChange(cb: (identity: Identity) => void): () => void {
  identityListeners.add(cb);

  if (identityCache) {
    queueMicrotask(() => {
      if (identityListeners.has(cb) && identityCache) {
        cb(identityCache);
      }
    });
  } else {
    void currentIdentity().catch(() => {
      // Consumers that need errors should call current() directly.
    });
  }

  return () => {
    identityListeners.delete(cb);
  };
}

class RealtimeClient {
  private socket: WebSocket | null = null;
  private openPromise: Promise<void> | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectAttempt = 0;
  private state: RealtimeConnectionState = 'disconnected';
  private readonly queued: RealtimeEnvelope[] = [];
  private readonly channelSince = new Map<string, string | undefined>();
  private readonly listeners = new Map<string, RealtimeListenerMap>();
  private readonly stateListeners = new Set<RealtimeStateCallback>();

  onStateChange(cb: RealtimeStateCallback): () => void {
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

  on(channel: string, event: string, cb: RealtimeCallback, since?: string): () => void {
    requireId(channel, 'channel');
    requireId(event, 'event');

    let events = this.listeners.get(channel);
    if (!events) {
      events = new Map<string, Set<RealtimeCallback>>();
      this.listeners.set(channel, events);
    }

    let callbacks = events.get(event);
    if (!callbacks) {
      callbacks = new Set<RealtimeCallback>();
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

  subscribe(channel: string, since?: string): void {
    requireId(channel, 'channel');

    if (!this.channelSince.has(channel)) {
      this.channelSince.set(channel, since);
      const socket = this.ensureSocket();
      if (socket.readyState === WebSocket.OPEN) {
        this.sendNow({ type: 'subscribe', channel, since });
      }
      return;
    }

    if (since && !this.channelSince.get(channel)) {
      this.channelSince.set(channel, since);
    }
  }

  publish(channel: string, event: string, data?: unknown): void {
    requireId(channel, 'channel');
    requireId(event, 'event');
    this.sendEnvelope({ type: 'publish', channel, event, data });
  }

  async open(): Promise<void> {
    const socket = this.ensureSocket();
    if (socket.readyState === WebSocket.OPEN) {
      return;
    }
    if (!this.openPromise) {
      this.openPromise = new Promise<void>((resolve, reject) => {
        const onOpen = (): void => {
          cleanup();
          resolve();
        };
        const onError = (): void => {
          cleanup();
          reject(new Error('OpenQuick realtime connection failed'));
        };
        const cleanup = (): void => {
          socket.removeEventListener('open', onOpen);
          socket.removeEventListener('error', onError);
        };
        socket.addEventListener('open', onOpen);
        socket.addEventListener('error', onError);
      });
    }
    return this.openPromise;
  }

  private ensureSocket(): WebSocket {
    if (typeof WebSocket === 'undefined') {
      throw new Error('OpenQuick realtime requires WebSocket support');
    }

    if (this.socket && this.socket.readyState !== WebSocket.CLOSED) {
      return this.socket;
    }

    const socket = new WebSocket(realtimeURL('realtime'));
    this.socket = socket;
    this.openPromise = null;
    this.setState(this.state === 'reconnecting' ? 'reconnecting' : 'connecting');

    socket.addEventListener('open', () => {
      this.reconnectAttempt = 0;
      this.clearReconnectTimer();
      this.setState('connected');
      for (const [channel, since] of this.channelSince) {
        this.sendNow({ type: 'subscribe', channel, since });
      }
      while (this.queued.length > 0 && socket.readyState === WebSocket.OPEN) {
        this.sendNow(this.queued.shift() as RealtimeEnvelope);
      }
    });

    socket.addEventListener('message', (event) => {
      this.handleMessage(event.data);
    });

    socket.addEventListener('close', () => {
      if (this.socket === socket) {
        this.socket = null;
        this.openPromise = null;
        this.setState('disconnected');
        if (this.hasActiveWork()) {
          this.scheduleReconnect();
        }
      }
    });

    return socket;
  }

  private hasActiveWork(): boolean {
    return this.listeners.size > 0 || this.queued.length > 0 || this.channelSince.size > 0;
  }

  private setState(state: RealtimeConnectionState): void {
    if (this.state === state) {
      return;
    }
    this.state = state;
    for (const listener of Array.from(this.stateListeners)) {
      listener(state);
    }
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer !== null) {
      return;
    }
    this.setState('reconnecting');
    const delay = Math.min(30_000, 1_000 * 2 ** Math.min(this.reconnectAttempt, 5));
    this.reconnectAttempt += 1;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.hasActiveWork()) {
        this.setState('disconnected');
        return;
      }
      try {
        this.ensureSocket();
      } catch {
        this.scheduleReconnect();
      }
    }, delay);
  }

  private sendEnvelope(envelope: RealtimeEnvelope): void {
    const socket = this.ensureSocket();
    if (socket.readyState === WebSocket.OPEN) {
      this.sendNow(envelope);
      return;
    }
    this.queued.push(envelope);
  }

  private sendNow(envelope: RealtimeEnvelope): void {
    this.socket?.send(JSON.stringify(envelope));
  }

  private handleMessage(raw: unknown): void {
    let envelope: RealtimeEnvelope;
    try {
      envelope = JSON.parse(String(raw)) as RealtimeEnvelope;
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

    const callbacks = new Set<RealtimeCallback>([
      ...(events.get(envelope.event) || []),
      ...(events.get('*') || []),
    ]);

    for (const callback of callbacks) {
      callback(envelope.data, envelope);
    }
  }
}

const realtimeClient = new RealtimeClient();

function collection<T = DocumentRecord>(name: string): Collection<T> {
  requireId(name, 'collection name');
  const base = apiPath('db', name);
  const channel = `db:${name}`;

  return {
    async create(data: Record<string, unknown>, options: RequestOptions = {}): Promise<T> {
      return normalizeDocument<T>(
        await requestJson<unknown>(base, {
          method: 'POST',
          headers: jsonHeaders,
          body: JSON.stringify(data),
          signal: options.signal,
        }),
      );
    },

    async get(id: string, options: RequestOptions = {}): Promise<T> {
      return normalizeDocument<T>(await requestJson<unknown>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`, { signal: options.signal }));
    },

    async update(id: string, patch: Record<string, unknown>, options: DbWriteOptions = {}): Promise<T> {
      return normalizeDocument<T>(
        await requestJson<unknown>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`, {
          method: 'PATCH',
          headers: dbWriteHeaders(options, jsonHeaders),
          body: JSON.stringify(patch),
          signal: options.signal,
        }),
      );
    },

    async remove(id: string, options: DbWriteOptions = {}): Promise<void> {
      await requestJson<void>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`, {
        method: 'DELETE',
        headers: dbWriteHeaders(options),
        signal: options.signal,
      });
    },

    async list(options: DbListOptions = {}): Promise<ListResult<T>> {
      const raw = await requestJson<unknown[] | { items?: unknown[]; documents?: unknown[]; next_cursor?: string; nextCursor?: string }>(dbListPath(base, options), { signal: options.signal });
      return normalizeListResult<T>(raw);
    },

    async subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void> {
      const unsubs: Array<() => void> = [];

      if (handlers.onCreate) {
        unsubs.push(realtimeClient.on(channel, 'create', (data) => handlers.onCreate?.(normalizeDocument<T>(data)), handlers.since));
      }
      if (handlers.onUpdate) {
        unsubs.push(realtimeClient.on(channel, 'update', (data) => handlers.onUpdate?.(normalizeDocument<T>(data)), handlers.since));
      }
      if (handlers.onDelete) {
        unsubs.push(
          realtimeClient.on(
            channel,
            'delete',
            (data) => handlers.onDelete?.(typeof data === 'string' ? data : String((data as { id?: unknown } | null)?.id || '')),
            handlers.since,
          ),
        );
      }

      realtimeClient.subscribe(channel, handlers.since);
      await realtimeClient.open();

      return () => {
        for (const unsub of unsubs.splice(0)) {
          unsub();
        }
      };
    },
  };
}

function progressBody(file: Blob, onProgress: (progress: UploadProgress) => void): BodyInit | null {
  if (typeof ReadableStream === 'undefined' || typeof file.stream !== 'function') {
    return null;
  }
  const total = typeof file.size === 'number' ? file.size : undefined;
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
    },
  }) as unknown as BodyInit;
}

async function putUpload(file: File | Blob, options: UploadOptions = {}): Promise<Upload> {
  const name = options.name || ('name' in file && typeof file.name === 'string' ? file.name : 'upload');
  if (options.onProgress) {
    const body = progressBody(file, options.onProgress);
    if (body) {
      const upload = await requestJson<Upload>(`${apiPath('uploads')}?name=${encodeURIComponent(name)}`, {
        method: 'POST',
        headers: { 'content-type': file.type || 'application/octet-stream' },
        body,
        signal: options.signal,
        ...(typeof navigator === 'undefined' ? { duplex: 'half' } : {}),
      } as RequestInit);
      return pathAwareURL(upload);
    }
  }
  if (typeof FormData === 'undefined') {
    throw new Error('OpenQuick uploads require FormData support');
  }

  const form = new FormData();
  form.append('file', file, name);
  form.append('name', name);

  const upload = await requestJson<Upload>(apiPath('uploads'), {
    method: 'POST',
    body: form,
    signal: options.signal,
  });
  return pathAwareURL(upload);
}

async function getUpload(id: string, options: RequestOptions = {}): Promise<Upload> {
  const upload = await requestJson<Upload>(apiPath('uploads', requireId(id, 'upload id')), { signal: options.signal });
  return pathAwareURL(upload);
}

async function removeUpload(id: string, options: RequestOptions = {}): Promise<void> {
  await requestJson<void>(apiPath('uploads', requireId(id, 'upload id')), {
    method: 'DELETE',
    signal: options.signal,
  });
}

async function listUploads(options: UploadListOptions = {}): Promise<UploadListResult> {
  const params = new URLSearchParams();
  if (options.limit !== undefined) params.set('limit', String(options.limit));
  if (options.cursor) params.set('cursor', options.cursor);
  const url = apiPath('uploads') + (params.toString() ? `?${params}` : '');
  const envelope = await requestJson<{ uploads?: Upload[]; next_cursor?: string }>(url, {
    signal: options.signal,
  });
  const uploads = (envelope.uploads || []).map(pathAwareURL) as UploadListResult;
  uploads.uploads = [...uploads];
  uploads.next_cursor = envelope.next_cursor || undefined;
  uploads.nextCursor = uploads.next_cursor;
  return uploads;
}

function realtimeChannel(name: string): Channel {
  requireId(name, 'channel name');

  return {
    on(event: string, cb: (data: unknown, envelope: RealtimeEnvelope) => void): () => void {
      return realtimeClient.on(name, event, cb);
    },
    send(event: string, data?: unknown): void {
      realtimeClient.publish(name, event, data);
    },
  };
}

function fetchCapabilities(options: RequestOptions = {}): Promise<Capabilities> {
  return requestJson<Capabilities>(apiPath('capabilities'), { signal: options.signal });
}

async function requireCapability(apiName: string, capability: string, options: RequestOptions = {}): Promise<void> {
  let capabilities: Capabilities;
  try {
    capabilities = await fetchCapabilities(options);
  } catch (error) {
    if (error instanceof OpenQuickError && error.code === 'aborted') {
      throw error;
    }
    throw new Error(`${apiName} is not available on this host (capabilities check failed: ${errorMessage(error)})`);
  }

  if (capabilities[capability] !== true) {
    throw new Error(`${apiName} is not available on this host`);
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function extractDeltaText(payload: unknown): string {
  if (typeof payload === 'string') {
    return payload;
  }
  if (!isRecord(payload)) {
    return '';
  }

  const delta = payload.delta;
  if (typeof delta === 'string') {
    return delta;
  }
  if (isRecord(delta) && typeof delta.content === 'string') {
    return delta.content;
  }
  return '';
}

interface ChatStreamState {
  content: string;
  lastPayload: Record<string, unknown> | null;
}

function processChatStreamEvent(rawEvent: string, state: ChatStreamState, onDelta?: (delta: string, event: unknown) => void): boolean {
  const data = rawEvent
    .split(/\r?\n/)
    .filter((line) => line.startsWith('data:'))
    .map((line) => {
      const value = line.slice(5);
      return value.startsWith(' ') ? value.slice(1) : value;
    })
    .join('\n');

  if (data.trim().length === 0) {
    return false;
  }

  const trimmed = data.trim();
  if (trimmed === '[DONE]' || trimmed.toLowerCase() === 'done') {
    return true;
  }

  let payload: unknown = data;
  try {
    payload = JSON.parse(data) as unknown;
  } catch {
    // Treat non-JSON data as a raw text delta.
  }

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

function buildStreamChatResponse(state: ChatStreamState, fallbackModel: unknown): ChatResponse {
  const response: Record<string, unknown> = state.lastPayload ? { ...state.lastPayload } : {};
  const existingMessage = isRecord(response.message) ? response.message : {};
  const content = typeof existingMessage.content === 'string' && existingMessage.content.length > 0 ? existingMessage.content : state.content;

  response.message = {
    role: typeof existingMessage.role === 'string' ? existingMessage.role : 'assistant',
    ...existingMessage,
    content,
  };

  if (typeof response.model !== 'string' && typeof fallbackModel === 'string') {
    response.model = fallbackModel;
  }

  return response as ChatResponse;
}

async function requestChatStream(body: Record<string, unknown>, fallbackModel: unknown, onDelta?: (delta: string, event: unknown) => void, signal?: AbortSignal): Promise<ChatResponse> {
  let response: Response;
  try {
    response = await fetch(apiPath('ai', 'chat'), {
      method: 'POST',
      headers: {
        accept: 'text/event-stream, application/json',
        'content-type': 'application/json',
      },
      body: JSON.stringify(body),
      credentials: 'same-origin',
      signal,
    });
  } catch (err) {
    if (isAbortError(err)) throwAbortError(err);
    throw err;
  }

  if (!response.ok) {
    await throwRequestError(response);
  }

  const contentType = response.headers.get('content-type') || '';
  if (!response.body || !contentType.includes('text/event-stream')) {
    const parsed = await parseResponse<ChatResponse>(response);
    const content = isRecord(parsed.message) && typeof parsed.message.content === 'string' ? parsed.message.content : '';
    if (content.length > 0) {
      onDelta?.(content, parsed);
    }
    return parsed;
  }

  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  const state: ChatStreamState = { content: '', lastPayload: null };
  let buffer = '';
  let sawDone = false;

  while (!sawDone) {
    const chunk = await reader.read();
    if (chunk.done) {
      break;
    }

    buffer += decoder.decode(chunk.value, { stream: true });
    const events = buffer.split(/\r?\n\r?\n/);
    buffer = events.pop() || '';
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
    await reader.cancel().catch(() => {
      // Ignore stream shutdown races after the done sentinel.
    });
  }

  return buildStreamChatResponse(state, fallbackModel);
}

async function chat(messages: ChatMessage[], options: ChatOptions = {}): Promise<ChatResponse> {
  await requireCapability('quick.ai.chat', 'ai', { signal: options.signal });

  const { model, stream, signal, onDelta, ...rest } = options;
  const useStream = stream === true || typeof onDelta === 'function';
  const body: Record<string, unknown> = {
    ...rest,
    messages,
    stream: useStream,
  };
  if (model !== undefined) {
    body.model = model;
  }

  if (useStream) {
    return requestChatStream(body, model, onDelta, signal);
  }

  return requestJson<ChatResponse>(apiPath('ai', 'chat'), {
    method: 'POST',
    headers: jsonHeaders,
    body: JSON.stringify(body),
    signal,
  });
}

async function image(prompt: string, options: ImageOptions = {}): Promise<ImageResponse> {
  requireId(prompt, 'prompt');
  await requireCapability('quick.ai.image', 'ai', { signal: options.signal });

  const { model, size, signal, ...rest } = options;
  const body: Record<string, unknown> = { ...rest, prompt };
  if (model !== undefined) {
    body.model = model;
  }
  if (size !== undefined) {
    body.size = size;
  }

  const image = await requestJson<ImageResponse>(apiPath('ai', 'images'), {
    method: 'POST',
    headers: jsonHeaders,
    body: JSON.stringify(body),
    signal,
  });
  return pathAwareURL(image);
}

async function warehouseMetadata(options: RequestOptions = {}): Promise<WarehouseMetadataResult> {
  await requireCapability('quick.warehouse.metadata', 'warehouse', options);
  return requestJson<WarehouseMetadataResult>(apiPath('warehouse'), { signal: options.signal });
}

async function warehouseQuery(name: string, params: WarehouseParams = {}, options: RequestOptions = {}): Promise<WarehouseQueryResult> {
  requireId(name, 'warehouse query name');
  await requireCapability('quick.warehouse.query', 'warehouse', options);

  return requestJson<WarehouseQueryResult>(apiPath('warehouse', name), {
    method: 'POST',
    headers: jsonHeaders,
    body: JSON.stringify(params),
    signal: options.signal,
  });
}

export const quick = {
  identity: {
    current: currentIdentity,
    onChange: onIdentityChange,
  },
  db: {
    collection,
  },
  uploads: {
    put: putUpload,
    get: getUpload,
    list: listUploads,
    remove: removeUpload,
  },
  realtime: {
    channel: realtimeChannel,
    onStateChange: (cb: RealtimeStateCallback): (() => void) => realtimeClient.onStateChange(cb),
  },
  ai: {
    chat,
    image,
  },
  warehouse: {
    metadata: warehouseMetadata,
    query: warehouseQuery,
  },
  capabilities: fetchCapabilities,
};

export default quick;
