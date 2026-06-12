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

The SDK uses relative `/_quick/*` URLs only. It has no runtime dependencies.

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

## Capabilities and AI

Use capabilities to adapt to v0/v1/v2 hosts:

```js
const caps = await quick.capabilities();
if (caps.db) {
  // enable DB-backed UI
}
```

AI is intentionally host-gated. In the v1 SDK shape, `quick.ai.chat()` checks `/_quick/capabilities` and then throws a clear `quick.ai.chat is not available on this host` error unless a future host enables AI.
