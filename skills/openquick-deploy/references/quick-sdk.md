# OpenQuick JavaScript SDK reference

Import the SDK from the same origin as the hosted site:

```js
import { quick } from '/_quick/sdk.js';
```

The SDK only calls relative `/_quick/*` URLs with same-origin credentials.

## Surface

```ts
export const quick = {
  identity: {
    current(): Promise<Identity>;
    onChange(cb: (identity: Identity) => void): () => void;
  },
  db: {
    collection<T = DocumentRecord>(name: string): Collection<T>;
  },
  uploads: {
    put(file: File | Blob, options?: UploadOptions): Promise<Upload>;
    get(id: string): Promise<Upload>;
    remove(id: string): Promise<void>;
  },
  realtime: {
    channel(name: string): Channel;
  },
  ai: {
    chat(messages: ChatMessage[], options?: ChatOptions): Promise<ChatResponse>;
    image(prompt: string, options?: ImageOptions): Promise<ImageResponse>;
  },
  warehouse: {
    query(name: string, params?: Record<string, unknown>): Promise<WarehouseQueryResult>;
  },
  capabilities(): Promise<Capabilities>;
};
```

## Identity

```ts
type Identity = {
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
};
```

```js
const me = await quick.identity.current();
const stop = quick.identity.onChange((identity) => {
  console.log(identity.email || identity.login || identity.subject);
});
```

`current()` fetches `GET /_quick/identity` once per page lifetime. `onChange()` returns an unsubscribe function.

## Capabilities

```js
const caps = await quick.capabilities();
if (caps.db) enableDbUi();
if (caps.ai) enableAiUi();
if (caps.warehouse) enableReportsUi();
```

AI and warehouse calls check capabilities first. If a host reports the feature as false or omits it, the SDK throws a clear not-available error before calling the feature endpoint.

## Document DB

```ts
type DocumentRecord = { id: string; [key: string]: unknown };

type Collection<T = DocumentRecord> = {
  create(data: Record<string, unknown>): Promise<T>;
  get(id: string): Promise<T>;
  update(id: string, patch: Record<string, unknown>): Promise<T>;
  remove(id: string): Promise<void>;
  list(): Promise<T[]>;
  subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void>;
};

type DbSubscriptionHandlers<T> = {
  since?: string;
  onCreate?: (doc: T) => void;
  onUpdate?: (doc: T) => void;
  onDelete?: (id: string) => void;
};
```

```js
const posts = quick.db.collection('posts');
const created = await posts.create({ title: 'Hello', status: 'draft' });
const post = await posts.get(created.id);
await posts.update(created.id, { status: 'published' });
const all = await posts.list();
await posts.remove(created.id);
```

HTTP backing:

```text
GET    /_quick/db/:collection
POST   /_quick/db/:collection
GET    /_quick/db/:collection/:id
PATCH  /_quick/db/:collection/:id
DELETE /_quick/db/:collection/:id
```

Subscriptions use `/_quick/realtime` and channel `db:<collection>`:

```js
const unsubscribe = await posts.subscribe({
  onCreate: (doc) => console.log('created', doc),
  onUpdate: (doc) => console.log('updated', doc),
  onDelete: (id) => console.log('deleted', id),
});
```

## Realtime

```ts
type RealtimeEnvelope = {
  type: 'subscribe' | 'publish';
  channel: string;
  since?: string;
  event?: string;
  data?: unknown;
};

type Channel = {
  on(event: string, cb: (data: unknown, envelope: RealtimeEnvelope) => void): () => void;
  send(event: string, data?: unknown): void;
};
```

```js
const room = quick.realtime.channel('lunch-room');
const stop = room.on('cursor', (data) => renderCursor(data));
room.send('cursor', { x: 120, y: 90 });
```

## Uploads

```ts
type UploadOptions = { name?: string };
type Upload = {
  id: string;
  url: string;
  name?: string;
  content_type?: string;
  size?: number;
  created_at?: string;
};
```

```js
const upload = await quick.uploads.put(file, { name: file.name });
const metadata = await quick.uploads.get(upload.id);
await quick.uploads.remove(upload.id);
```

HTTP backing:

```text
POST   /_quick/uploads
GET    /_quick/uploads/:id
DELETE /_quick/uploads/:id
```

## AI

AI is host-gated. Hosts report `capabilities().ai === true` only when configured.

```ts
type ChatMessage = {
  role: 'system' | 'user' | 'assistant' | string;
  content: string;
};

type ChatOptions = {
  model?: string;
  stream?: boolean;
  onDelta?: (delta: string, event: unknown) => void;
};

type ChatResponse = {
  id?: string;
  model?: string;
  message: ChatMessage;
  usage?: { prompt_tokens?: number; completion_tokens?: number };
};

type ImageOptions = { model?: string; size?: string };
type ImageResponse = { id: string; model?: string; url: string };
```

Chat:

```js
const answer = await quick.ai.chat([
  { role: 'system', content: 'Be concise.' },
  { role: 'user', content: 'Summarize this page.' },
], { model: 'host-default' });
```

Streaming chat:

```js
let text = '';
const final = await quick.ai.chat(
  [{ role: 'user', content: 'Draft release notes.' }],
  {
    stream: true,
    onDelta(delta) {
      text += delta;
      output.textContent = text;
    },
  },
);
```

Image:

```js
const image = await quick.ai.image('A flat icon of a rocket', {
  model: 'host-default-image',
  size: '1024x1024',
});
preview.src = image.url;
```

HTTP backing:

```text
POST /_quick/ai/chat
POST /_quick/ai/images
```

## Warehouse

Warehouse is host-gated. Hosts report `capabilities().warehouse === true` only when named queries are configured.

```ts
type WarehouseQueryResult = {
  name: string;
  columns: string[];
  rows: unknown[][];
  row_count: number;
  truncated?: boolean;
};
```

```js
const report = await quick.warehouse.query('recent_orders', {
  status: 'paid',
  limit: 20,
});

const rows = report.rows.map((row) =>
  Object.fromEntries(report.columns.map((column, index) => [column, row[index]])),
);
```

HTTP backing:

```text
POST /_quick/warehouse/:name
```

Hosts may also support `GET /_quick/warehouse/:name` with query parameters, but browser code should prefer the SDK call.
