# OpenQuick JavaScript SDK reference

Import the SDK from the same origin as the hosted site. The SDK uses leading-slash `/_quick/*` paths and `same-origin` credentials.

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';

  console.log(await quick.identity.current());
</script>
```

## Top-level surface

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

## Capabilities pattern

Check host capabilities before rendering optional UI. AI and warehouse SDK calls also enforce their capability check before hitting feature endpoints.

```js
const caps = await quick.capabilities();

if (caps.db === true) {
  console.log('DB UI can be enabled');
}

if (caps.ai === true) {
  console.log('AI UI can be enabled');
}

if (caps.warehouse !== true) {
  console.log('Hide reports UI on this host');
}
```

If a host does not advertise AI or warehouse, calls fail early with errors such as `quick.ai.chat is not available on this host` or `quick.warehouse.query is not available on this host`.

## Identity

`quick.identity.current()` fetches `GET /_quick/identity` once and caches it for the page lifetime. `onChange()` registers a page-local listener and returns an unsubscribe function.

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
console.log(me.email || me.login || me.subject);

const stopIdentity = quick.identity.onChange((identity) => {
  console.log('identity changed', identity.provider, identity.subject);
});

stopIdentity();
```

Provider fields depend on the host IAP. Do not parse Cloudflare or Tailscale headers in site code; use `quick.identity.current()`.

## Document DB collections

Collections are namespaced by the site on the host. Documents have an `id` plus JSON fields.

```ts
type DocumentRecord = {
  id: string;
  [key: string]: unknown;
};

type Collection<T = DocumentRecord> = {
  create(data: Record<string, unknown>): Promise<T>;
  get(id: string): Promise<T>;
  update(id: string, patch: Record<string, unknown>): Promise<T>;
  remove(id: string): Promise<void>;
  list(): Promise<T[]>;
  subscribe(handlers: DbSubscriptionHandlers<T>): Promise<() => void>;
};
```

Create, read, update, list, and remove records:

```js
const votes = quick.db.collection('votes');

const created = await votes.create({
  choice: 'ramen',
  by: 'sam@example.com',
  created_at: new Date().toISOString(),
});

const record = await votes.get(created.id);
console.log(record.choice);

const updated = await votes.update(created.id, { choice: 'tacos' });
console.log(updated.choice);

const allVotes = await votes.list();
console.log(allVotes.length);

await votes.remove(created.id);
```

HTTP backing:

```text
GET    /_quick/db/:collection
POST   /_quick/db/:collection
GET    /_quick/db/:collection/:id
PATCH  /_quick/db/:collection/:id
DELETE /_quick/db/:collection/:id
```

## DB subscriptions

DB subscriptions use the shared WebSocket at `/_quick/realtime` and the channel `db:<collection>`. `subscribe()` resolves to an unsubscribe function.

```ts
type DbSubscriptionHandlers<T> = {
  since?: string;
  onCreate?: (doc: T) => void;
  onUpdate?: (doc: T) => void;
  onDelete?: (id: string) => void;
};
```

```js
const votes = quick.db.collection('votes');

const stopVotes = await votes.subscribe({
  onCreate(doc) {
    console.log('created', doc.id);
  },
  onUpdate(doc) {
    console.log('updated', doc.id);
  },
  onDelete(id) {
    console.log('deleted', id);
  },
});

stopVotes();
```

## Realtime channels

Use realtime channels for small collaborative UI events. The SDK opens `/_quick/realtime`, subscribes to channels, and publishes JSON envelopes.

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

const stopCursor = room.on('cursor', (data, envelope) => {
  console.log(envelope.channel, data);
});

room.send('cursor', { x: 120, y: 90 });
stopCursor();
```

## Uploads

Uploads accept a `File` or `Blob`. The SDK sends a multipart form to `POST /_quick/uploads` and returns metadata including a URL served by quickd.

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

```html
<input id="asset" type="file">
<script type="module">
  import { quick } from '/_quick/sdk.js';

  const input = document.querySelector('#asset');
  const file = input.files[0];
  const upload = await quick.uploads.put(file, { name: file.name });

  console.log(upload.id, upload.url);

  const metadata = await quick.uploads.get(upload.id);
  console.log(metadata.size);

  await quick.uploads.remove(upload.id);
</script>
```

HTTP backing:

```text
POST   /_quick/uploads
GET    /_quick/uploads/:id
DELETE /_quick/uploads/:id
```

## AI chat

AI is host-config gated. Provider keys stay on the host. Browser code sends prompts only to same-origin OpenQuick endpoints.

```ts
type ChatMessage = {
  role: 'system' | 'user' | 'assistant' | string;
  content: string;
};

type ChatOptions = {
  model?: string;
  stream?: boolean;
  onDelta?: (delta: string, event: unknown) => void;
  [key: string]: unknown;
};

type ChatResponse = {
  id?: string;
  model?: string;
  message: ChatMessage;
  usage?: {
    prompt_tokens?: number;
    completion_tokens?: number;
    [key: string]: unknown;
  };
};
```

Non-streaming chat posts to `POST /_quick/ai/chat`:

```js
const answer = await quick.ai.chat([
  { role: 'system', content: 'Be concise.' },
  { role: 'user', content: 'Summarize this release.' },
], { model: 'host-default' });

console.log(answer.message.content);
```

Streaming chat uses the same endpoint with `stream: true`. The SDK reads server-sent events, calls `onDelta` for each text delta, and resolves with a final assembled `ChatResponse`.

```js
let text = '';

const final = await quick.ai.chat(
  [{ role: 'user', content: 'Draft a changelog entry.' }],
  {
    stream: true,
    onDelta(delta) {
      text += delta;
      console.log(text);
    },
  },
);

console.log(final.message.content);
```

Passing `onDelta` also selects streaming behavior even if `stream` is omitted.

## AI images

Image generation posts to `POST /_quick/ai/images` and returns an internal upload URL.

```ts
type ImageOptions = {
  model?: string;
  size?: string;
  [key: string]: unknown;
};

type ImageResponse = {
  id: string;
  model?: string;
  url: string;
};
```

```html
<img id="preview" alt="Generated badge">
<script type="module">
  import { quick } from '/_quick/sdk.js';

  const image = await quick.ai.image('A small launch badge in flat SVG style', {
    model: 'host-default-image',
    size: '1024x1024',
  });

  document.querySelector('#preview').src = image.url;
</script>
```

## Warehouse named queries

Warehouse is host-config gated. Query names are a host allowlist, not arbitrary SQL from the browser.

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

console.table(rows);
```

HTTP backing:

```text
POST /_quick/warehouse/:name
```

## Failure handling

Treat SDK calls as network and authorization boundaries. Handle unavailable host gates, IAP expiry, rate limits, and validation errors.

```js
try {
  const caps = await quick.capabilities();
  if (caps.ai === true) {
    const answer = await quick.ai.chat([{ role: 'user', content: 'Say hi.' }]);
    console.log(answer.message.content);
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
}
```
