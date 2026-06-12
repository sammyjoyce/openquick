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
  create(data: Record<string, unknown>): Promise<T>;
  get(id: string): Promise<T>;
  update(id: string, patch: Record<string, unknown>): Promise<T>;
  remove(id: string): Promise<void>;
  list(): Promise<T[]>;
  subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void>;
}

export interface UploadOptions {
  name?: string;
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

export interface ChatMessage {
  role: 'system' | 'user' | 'assistant' | string;
  content: string;
  [key: string]: unknown;
}

export interface ChatOptions {
  [key: string]: unknown;
}

export interface ChatResponse {
  [key: string]: unknown;
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

function apiPath(...parts: string[]): string {
  return `/_quick/${parts.map((part) => encodeURIComponent(part)).join('/')}`;
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

async function requestJson<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  headers.set('accept', 'application/json');

  const isFormData = typeof FormData !== 'undefined' && init.body instanceof FormData;
  if (init.body !== undefined && !headers.has('content-type') && !isFormData) {
    headers.set('content-type', 'application/json');
  }

  const response = await fetch(path, {
    ...init,
    headers,
    credentials: 'same-origin',
  });

  if (!response.ok) {
    let details = response.statusText;
    try {
      const body = await parseResponse<unknown>(response);
      if (typeof body === 'string' && body.length > 0) {
        details = body;
      } else if (body && typeof body === 'object' && 'error' in body) {
        details = String((body as { error: unknown }).error);
      }
    } catch {
      // Keep the HTTP status text when the error body cannot be parsed.
    }
    throw new Error(`OpenQuick request failed: ${response.status} ${details}`);
  }

  return parseResponse<T>(response);
}

function normalizeList<T>(value: T[] | { items?: T[] }): T[] {
  if (Array.isArray(value)) {
    return value;
  }
  if (value && typeof value === 'object' && Array.isArray(value.items)) {
    return value.items;
  }
  return [];
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
  private readonly queued: RealtimeEnvelope[] = [];
  private readonly channelSince = new Map<string, string | undefined>();
  private readonly listeners = new Map<string, RealtimeListenerMap>();

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

    const socket = new WebSocket('/_quick/realtime');
    this.socket = socket;
    this.openPromise = null;

    socket.addEventListener('open', () => {
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
      }
    });

    return socket;
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
    create(data: Record<string, unknown>): Promise<T> {
      return requestJson<T>(base, {
        method: 'POST',
        headers: jsonHeaders,
        body: JSON.stringify(data),
      });
    },

    get(id: string): Promise<T> {
      return requestJson<T>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`);
    },

    update(id: string, patch: Record<string, unknown>): Promise<T> {
      return requestJson<T>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`, {
        method: 'PATCH',
        headers: jsonHeaders,
        body: JSON.stringify(patch),
      });
    },

    async remove(id: string): Promise<void> {
      await requestJson<void>(`${base}/${encodeURIComponent(requireId(id, 'document id'))}`, {
        method: 'DELETE',
      });
    },

    async list(): Promise<T[]> {
      return normalizeList<T>(await requestJson<T[] | { items?: T[] }>(base));
    },

    async subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void> {
      const unsubs: Array<() => void> = [];

      if (handlers.onCreate) {
        unsubs.push(realtimeClient.on(channel, 'create', (data) => handlers.onCreate?.(data as T), handlers.since));
      }
      if (handlers.onUpdate) {
        unsubs.push(realtimeClient.on(channel, 'update', (data) => handlers.onUpdate?.(data as T), handlers.since));
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

async function putUpload(file: File | Blob, options: UploadOptions = {}): Promise<Upload> {
  if (typeof FormData === 'undefined') {
    throw new Error('OpenQuick uploads require FormData support');
  }

  const name = options.name || ('name' in file && typeof file.name === 'string' ? file.name : 'upload');
  const form = new FormData();
  form.append('file', file, name);
  form.append('name', name);

  return requestJson<Upload>(apiPath('uploads'), {
    method: 'POST',
    body: form,
  });
}

function getUpload(id: string): Promise<Upload> {
  return requestJson<Upload>(apiPath('uploads', requireId(id, 'upload id')));
}

async function removeUpload(id: string): Promise<void> {
  await requestJson<void>(apiPath('uploads', requireId(id, 'upload id')), {
    method: 'DELETE',
  });
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

function fetchCapabilities(): Promise<Capabilities> {
  return requestJson<Capabilities>(apiPath('capabilities'));
}

async function chat(messages: ChatMessage[], options: ChatOptions = {}): Promise<ChatResponse> {
  void messages;
  void options;

  try {
    await fetchCapabilities();
  } catch (error) {
    throw new Error(`quick.ai.chat is not available on this host (capabilities check failed: ${errorMessage(error)})`);
  }

  throw new Error('quick.ai.chat is not available on this host');
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
    remove: removeUpload,
  },
  realtime: {
    channel: realtimeChannel,
  },
  ai: {
    chat,
  },
  capabilities: fetchCapabilities,
};

export default quick;
