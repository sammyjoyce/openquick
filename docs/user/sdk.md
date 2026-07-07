# OpenQuick JavaScript SDK

Hosted sites import the SDK from the same origin:

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';

  const me = await quick.identity.current();
  const votes = quick.db.collection('votes');
  await votes.create({ choice: 'ramen', by: me.email });
</script>
```

The SDK has no runtime dependencies.
It calls same-origin `/_quick/*` APIs on wildcard site hosts and automatically switches to `/~/site/_quick/*` when the current page is served through OpenQuick's path fallback.

## Top-level API

```ts
type RequestOptions = { signal?: AbortSignal };
type DbWriteOptions = RequestOptions & { revision?: string };
type DbListOptions = RequestOptions & {
  limit?: number;
  cursor?: string;
  filter?: Record<string, unknown> | string;
  sort?: string;
};
type UploadListOptions = RequestOptions & { limit?: number; cursor?: string };

export const quick = {
  identity: {
    current(): Promise<Identity>;
    onChange(cb: (identity: Identity) => void): () => void;
  },
  db: {
    collection(name: string): Collection;
  },
  uploads: {
    put(file: File | Blob, options?: UploadOptions): Promise<Upload>;
    get(id: string, options?: RequestOptions): Promise<Upload>;
    list(options?: UploadListOptions): Promise<UploadListResult>;
    remove(id: string, options?: RequestOptions): Promise<void>;
  },
  realtime: {
    channel(name: string): Channel;
    onStateChange(cb: (state: 'disconnected' | 'connecting' | 'connected' | 'reconnecting') => void): () => void;
  },
  ai: {
    chat(messages: ChatMessage[], options?: ChatOptions): Promise<ChatResponse>;
    image(prompt: string, options?: ImageOptions): Promise<ImageResponse>;
  },
  warehouse: {
    metadata(options?: RequestOptions): Promise<WarehouseMetadataResult>;
    query(name: string, params?: Record<string, unknown>, options?: RequestOptions): Promise<WarehouseQueryResult>;
  },
  capabilities(options?: RequestOptions): Promise<Capabilities>;
};

interface Collection<T = DocumentRecord> {
  create(data: Record<string, unknown>, options?: RequestOptions): Promise<T>;
  get(id: string, options?: RequestOptions): Promise<T>;
  update(id: string, patch: Record<string, unknown>, options?: DbWriteOptions): Promise<T>;
  remove(id: string, options?: DbWriteOptions): Promise<void>;
  list(options?: DbListOptions): Promise<ListResult<T>>;
  subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void>;
}
```

## Identity

```js
const me = await quick.identity.current();
const stop = quick.identity.onChange((identity) => {
  console.log(identity.email || identity.login || identity.subject);
});
```

`current()` fetches `GET /_quick/identity` once and caches the result for the page lifetime. `onChange()` returns an unsubscribe function.

Normalized identity fields include:

```json
{
  "authenticated": true,
  "provider": "cloudflare",
  "subject": "cloudflare:abc123",
  "email": "sam@example.com",
  "login": "sam@example.com",
  "name": "Sam Example",
  "avatar_url": "https://...",
  "groups": ["engineering"]
}
```

## Document DB

```js
const posts = quick.db.collection('posts');

const created = await posts.create({
  title: 'Hello OpenQuick',
  status: 'draft',
  created_at: new Date().toISOString(),
});

const post = await posts.get(created.id);
const published = await posts.update(created.id, { status: 'published' }, { revision: created.revision }); // PATCH semantics + If-Match
const listOptions = {
  limit: 25,
  filter: { status: 'published' },
  sort: '-updated_at',
};
const page = await posts.list(listOptions);
const nextPage = page.nextCursor ? await posts.list({ ...listOptions, cursor: page.nextCursor }) : [];
await posts.remove(created.id, { revision: published.revision });
```

HTTP backing:

```text
GET    /_quick/db/:collection?limit=&cursor=&filter=&sort=
POST   /_quick/db/:collection
GET    /_quick/db/:collection/:id
PATCH  /_quick/db/:collection/:id        # optional If-Match: <revision>
DELETE /_quick/db/:collection/:id        # optional If-Match: <revision>
```

`list()` accepts `limit`, `cursor`, `filter`, and `sort`.
Object filters are JSON-encoded; sort is a single key (`created_at`, `updated_at`, or `id`) with optional leading `-` for descending order.
Results are arrays with `documents`, `next_cursor`, and `nextCursor` aliases for pagination metadata.

`update()` and `remove()` accept `{ revision }` to make writes conditional. The SDK sends the value as `If-Match`; stale revisions fail with `OpenQuickError` status `409` and code `revision_mismatch`.

All records are namespaced by the site and authenticated identity on the host.
quickd returns document data in a `data` envelope; the SDK flattens it to `id` plus fields while keeping server metadata such as `id`, `revision`,
`created_at`, `updated_at`, and `created_by` authoritative over same-named user data fields.

## DB subscriptions

```js
const unsubscribe = await posts.subscribe({
  onCreate: (doc) => console.log('created', doc),
  onUpdate: (doc) => console.log('updated', doc),
  onDelete: (id) => console.log('deleted', id),
});
```

Subscriptions use the shared WebSocket at `/_quick/realtime` and the channel `db:<collection>`. Envelopes are JSON:

```json
{
  "type": "subscribe",
  "channel": "db:posts",
  "since": "optional-cursor"
}
```

Server publish messages use:

```json
{
  "type": "publish",
  "channel": "db:posts",
  "event": "create",
  "data": { "id": "post_123" }
}
```

## Realtime channels

```js
const stopState = quick.realtime.onStateChange((state) => console.log('realtime', state));
const room = quick.realtime.channel('lunch-room');
const stopCursor = room.on('cursor', (msg) => renderCursor(msg));
room.send('cursor', { x: 120, y: 90 });
```

`onStateChange()` reports `disconnected`, `connecting`, `connected`, and `reconnecting` for the shared socket and returns an unsubscribe function.
`send()` publishes this envelope over the shared connection:

```json
{
  "type": "publish",
  "channel": "lunch-room",
  "event": "cursor",
  "data": { "x": 120, "y": 90 }
}
```

## Uploads

```js
const upload = await quick.uploads.put(file, { name: file.name });
console.log(upload.url);

const metadata = await quick.uploads.get(upload.id);
const uploads = await quick.uploads.list({ limit: 20 });
const moreUploads = uploads.nextCursor ? await quick.uploads.list({ limit: 20, cursor: uploads.nextCursor }) : [];
await quick.uploads.remove(upload.id);
```

`list()` returns an array with `uploads`, `next_cursor`, and `nextCursor` aliases for pagination metadata.

HTTP backing:

```text
POST   /_quick/uploads
GET    /_quick/uploads?limit=&cursor=
GET    /_quick/uploads/:id
DELETE /_quick/uploads/:id
```

Files are served through `quickd` so identity, site namespace, content type, range requests, and quotas are enforced consistently.

## Capabilities

Use capabilities to adapt to v0/v1/v2 hosts:

```js
const caps = await quick.capabilities();
if (caps.db) {
  // enable DB-backed UI
}
if (caps.ai) {
  // enable AI UI
}
if (caps.warehouse) {
  // enable reporting UI
}
```

Feature-specific SDK calls check `GET /_quick/capabilities` first.
If a host reports `ai: false` or `warehouse: false` (or omits the flag), the call fails before hitting the feature endpoint with a clear error such as
`quick.ai.chat is not available on this host`.

## AI

Chat calls post same-origin JSON to `POST /_quick/ai/chat`:

```js
const answer = await quick.ai.chat([
  { role: 'system', content: 'Be concise.' },
  { role: 'user', content: 'Summarize this release.' },
], { model: 'host-default' });

console.log(answer.message.content);
```

Streaming uses the same endpoint with `stream: true`. The SDK reads server-sent events, assembles the assistant text, calls `onDelta` for each text delta, and resolves with a final `ChatResponse`:

```js
const chunks = [];
const final = await quick.ai.chat(
  [{ role: 'user', content: 'Draft a changelog entry.' }],
  {
    stream: true,
    onDelta(delta) {
      chunks.push(delta);
      render(chunks.join(''));
    },
  },
);
```

Image generation posts to `POST /_quick/ai/images` and returns an internal upload URL:

```js
const image = await quick.ai.image('A small launch badge in flat SVG style', {
  model: 'host-default-image',
  size: '1024x1024',
});

preview.src = image.url; // /_quick/uploads/<id> or /~/site/_quick/uploads/<id> on path fallback
```

Types:

```ts
type ChatMessage = { role: 'system' | 'user' | 'assistant' | string; content: string };
type ChatResponse = {
  id?: string;
  model?: string;
  message: ChatMessage;
  usage?: { prompt_tokens?: number; completion_tokens?: number };
};
type ImageResponse = { id: string; model?: string; url: string };
```

AI provider keys stay server-side. Browser code sends prompts only to the same-origin OpenQuick host.

## Warehouse

Warehouse metadata lists the configured named queries with their parameters, maximum row count, and columns:

```js
const metadata = await quick.warehouse.metadata(); // GET /_quick/warehouse
for (const query of metadata.queries) {
  console.log(query.name, query.params, query.columns);
}
```

Named warehouse queries post same-origin JSON to `POST /_quick/warehouse/:name`:

```js
const report = await quick.warehouse.query('recent_orders', {
  status: 'paid',
  limit: 20,
});

for (const row of report.rows) {
  console.log(Object.fromEntries(report.columns.map((column, index) => [column, row[index]])));
}
```

Response shapes:

```ts
type WarehouseMetadataResult = {
  queries: Array<{
    name: string;
    params: Array<{ name: string; type: string }>;
    max_rows: number;
    columns: string[];
  }>;
};

type WarehouseQueryResult = {
  name: string;
  columns: string[];
  rows: unknown[][];
  row_count: number;
  truncated?: boolean;
};
```

Warehouse access is host-gated by `capabilities().warehouse`; query names are server-configured and should be treated as an allowlist, not arbitrary SQL.
