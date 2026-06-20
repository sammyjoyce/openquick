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

The SDK has no runtime dependencies. It calls same-origin `/_quick/*` APIs on wildcard site hosts and automatically switches to `/~/site/_quick/*` when the current page is served through OpenQuick's path fallback.

## Top-level API

```ts
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
await posts.update(created.id, { status: 'published' }); // PATCH semantics
const allPosts = await posts.list();
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

All records are namespaced by the site and authenticated identity on the host.

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
const room = quick.realtime.channel('lunch-room');
const stopCursor = room.on('cursor', (msg) => renderCursor(msg));
room.send('cursor', { x: 120, y: 90 });
```

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
await quick.uploads.remove(upload.id);
```

HTTP backing:

```text
POST   /_quick/uploads
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

Feature-specific SDK calls check `GET /_quick/capabilities` first. If a host reports `ai: false` or `warehouse: false` (or omits the flag), the call fails before hitting the feature endpoint with a clear error such as `quick.ai.chat is not available on this host`.

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

Response shape:

```ts
type WarehouseQueryResult = {
  name: string;
  columns: string[];
  rows: unknown[][];
  row_count: number;
  truncated?: boolean;
};
```

Warehouse access is host-gated by `capabilities().warehouse`; query names are server-configured and should be treated as an allowlist, not arbitrary SQL.
