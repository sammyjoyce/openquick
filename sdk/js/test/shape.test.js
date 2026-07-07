import test from 'node:test';
import assert from 'node:assert/strict';
import { OpenQuickError, quick } from '../dist/quick.js';

test('OpenQuick SDK surface shape', () => {
  assert.equal(typeof quick, 'object');
  assert.equal(typeof quick.identity.current, 'function');
  assert.equal(typeof quick.identity.onChange, 'function');
  assert.equal(typeof quick.db.collection, 'function');
  assert.equal(typeof quick.uploads.put, 'function');
  assert.equal(typeof quick.uploads.get, 'function');
  assert.equal(typeof quick.uploads.list, 'function');
  assert.equal(typeof quick.uploads.remove, 'function');
  assert.equal(typeof quick.realtime.channel, 'function');
  assert.equal(typeof quick.realtime.onStateChange, 'function');
  assert.equal(typeof quick.capabilities, 'function');
  assert.equal(typeof quick.ai.chat, 'function');
  assert.equal(typeof quick.ai.image, 'function');
  assert.equal(typeof quick.warehouse.metadata, 'function');
  assert.equal(typeof quick.warehouse.query, 'function');

  const votes = quick.db.collection('votes');
  assert.equal(typeof votes.create, 'function');
  assert.equal(typeof votes.get, 'function');
  assert.equal(typeof votes.update, 'function');
  assert.equal(typeof votes.remove, 'function');
  assert.equal(typeof votes.list, 'function');
  assert.equal(typeof votes.subscribe, 'function');

  const channel = quick.realtime.channel('shape');
  assert.equal(typeof channel.on, 'function');
  assert.equal(typeof channel.send, 'function');
});

function jsonResponse(body) {
  return new Response(JSON.stringify(body), {
    status: 200,
    headers: { 'content-type': 'application/json' },
  });
}

test('db.list unwraps the quickd documents envelope and data nesting', async () => {
  const originalFetch = globalThis.fetch;
  let requestedPath = '';
  globalThis.fetch = async (path) => {
    requestedPath = String(path);
    return jsonResponse({
      documents: [
        {
          id: 'doc-1',
          data: { text: 'hello', by: 'sam@example.com' },
          created_by: 'dev:sam@example.com',
          created_at: '2026-01-01T00:00:00Z',
          updated_at: '2026-01-01T00:00:00Z',
        },
      ],
      next_cursor: 'cursor-2',
    });
  };
  try {
    const docs = await quick.db.collection('notes').list({ limit: 1, cursor: 'cursor-1', filter: { tag: 'work' }, sort: ['created_at', 'id'] });
    assert.equal(requestedPath.includes('limit=1'), true);
    assert.equal(requestedPath.includes('cursor=cursor-1'), true);
    assert.equal(requestedPath.includes('filter='), true);
    assert.equal(requestedPath.includes('sort=created_at%2Cid'), true);
    assert.equal(docs.length, 1);
    assert.notEqual(docs.documents, docs);
    assert.deepEqual(docs.documents, [...docs]);
    assert.doesNotThrow(() => JSON.stringify(docs));
    assert.equal(docs.next_cursor, 'cursor-2');
    assert.equal(docs.nextCursor, 'cursor-2');
    assert.equal(docs[0].id, 'doc-1');
    assert.equal(docs[0].text, 'hello');
    assert.equal(docs[0].by, 'sam@example.com');
    assert.equal(docs[0].created_by, 'dev:sam@example.com');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('db.list still accepts a bare array of flat documents', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => jsonResponse([{ id: 'doc-2', text: 'flat' }]);
  try {
    const docs = await quick.db.collection('notes').list();
    assert.equal(docs.length, 1);
    assert.equal(docs[0].id, 'doc-2');
    assert.equal(docs[0].text, 'flat');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('db document metadata is authoritative over same-named data fields', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () =>
    jsonResponse({
      id: 'doc-meta',
      revision: 'server-rev',
      created_at: '2026-01-01T00:00:00Z',
      data: {
        id: 'user-id',
        revision: 'user-rev',
        created_at: 'user-created-at',
        title: 'metadata wins',
      },
    });
  try {
    const doc = await quick.db.collection('notes').create({ title: 'metadata wins' });
    assert.equal(doc.id, 'doc-meta');
    assert.equal(doc.revision, 'server-rev');
    assert.equal(doc.created_at, '2026-01-01T00:00:00Z');
    assert.equal(doc.title, 'metadata wins');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('db.update and db.remove send revision preconditions as If-Match', async () => {
  const originalFetch = globalThis.fetch;
  const seen = [];
  globalThis.fetch = async (path, init = {}) => {
    const headers = new Headers(init.headers);
    seen.push({
      path: String(path),
      method: init.method,
      ifMatch: headers.get('if-match'),
      contentType: headers.get('content-type'),
      body: init.body,
    });
    if (init.method === 'PATCH') {
      return jsonResponse({ id: 'doc-5', revision: 'rev-2', data: { text: 'updated' } });
    }
    return new Response(null, { status: 204 });
  };
  try {
    const notes = quick.db.collection('notes');
    const updated = await notes.update('doc-5', { text: 'updated' }, { revision: 'rev-1' });
    await notes.remove('doc-5', { revision: 'rev-2' });

    assert.equal(updated.revision, 'rev-2');
    assert.equal(seen.length, 2);
    assert.equal(seen[0].method, 'PATCH');
    assert.equal(seen[0].ifMatch, 'rev-1');
    assert.equal(seen[0].contentType, 'application/json');
    assert.equal(seen[0].path.includes('revision='), false);
    assert.equal(seen[1].method, 'DELETE');
    assert.equal(seen[1].ifMatch, 'rev-2');
    assert.equal(seen[1].path.includes('revision='), false);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('stale DB revision preconditions throw typed revision_mismatch errors', async () => {
  const originalFetch = globalThis.fetch;
  let ifMatch = '';
  globalThis.fetch = async (_path, init = {}) => {
    ifMatch = new Headers(init.headers).get('if-match') || '';
    return new Response(JSON.stringify({ error: 'stale revision', code: 'revision_mismatch', revision: 'server-rev' }), {
      status: 409,
      headers: { 'content-type': 'application/json' },
    });
  };
  try {
    await assert.rejects(
      () => quick.db.collection('notes').update('doc-6', { text: 'stale' }, { revision: 'stale-rev' }),
      (error) => {
        assert.equal(error instanceof OpenQuickError, true);
        assert.equal(error.status, 409);
        assert.equal(error.code, 'revision_mismatch');
        assert.equal(error.details.revision, 'server-rev');
        return true;
      },
    );
    assert.equal(ifMatch, 'stale-rev');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('warehouse.metadata returns configured query metadata', async () => {
  const originalFetch = globalThis.fetch;
  const seen = [];
  globalThis.fetch = async (path) => {
    seen.push(String(path));
    if (String(path).includes('/capabilities')) return jsonResponse({ warehouse: true });
    return jsonResponse({ queries: [{ name: 'recent', params: [], max_rows: 100, columns: ['id'] }] });
  };
  try {
    const meta = await quick.warehouse.metadata();
    assert.equal(seen.some((p) => p.endsWith('/warehouse')), true);
    assert.equal(meta.queries[0].name, 'recent');
    assert.deepEqual(meta.queries[0].columns, ['id']);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('uploads.list unwraps envelope and pagination metadata', async () => {
  const originalFetch = globalThis.fetch;
  let requestedPath = '';
  globalThis.fetch = async (path) => {
    requestedPath = String(path);
    return jsonResponse({
      uploads: [{ id: 'u1', url: '/_quick/uploads/u1', name: 'a.txt', size: 3 }],
      next_cursor: 'u2',
    });
  };
  try {
    const uploads = await quick.uploads.list({ limit: 1, cursor: 'u0' });
    assert.equal(requestedPath.includes('/uploads?'), true);
    assert.equal(requestedPath.includes('limit=1'), true);
    assert.equal(requestedPath.includes('cursor=u0'), true);
    assert.equal(uploads.length, 1);
    assert.notEqual(uploads.uploads, uploads);
    assert.deepEqual(uploads.uploads, [...uploads]);
    assert.doesNotThrow(() => JSON.stringify(uploads));
    assert.equal(uploads.next_cursor, 'u2');
    assert.equal(uploads.nextCursor, 'u2');
    assert.equal(uploads[0].name, 'a.txt');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('db.create flattens the data envelope on single documents', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () =>
    jsonResponse({
      id: 'doc-3',
      data: { choice: 'ramen' },
      created_by: 'dev:sam@example.com',
    });
  try {
    const doc = await quick.db.collection('votes').create({ choice: 'ramen' });
    assert.equal(doc.id, 'doc-3');
    assert.equal(doc.choice, 'ramen');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('db.create preserves a data id when the parent envelope omits id', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () =>
    jsonResponse({
      data: { id: 'doc-4', choice: 'tea' },
      created_by: 'dev:sam@example.com',
    });
  try {
    const doc = await quick.db.collection('votes').create({ choice: 'tea' });
    assert.equal(doc.id, 'doc-4');
    assert.equal(doc.choice, 'tea');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('aborted requests throw typed OpenQuickError', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => {
    const err = new Error('cancelled');
    err.name = 'AbortError';
    throw err;
  };
  try {
    await assert.rejects(
      () => quick.ai.chat([{ role: 'user', content: 'stop' }], { signal: new AbortController().signal }),
      (err) => {
        assert.equal(err instanceof OpenQuickError, true);
        assert.equal(err.code, 'aborted');
        assert.equal(err.status, 0);
        return true;
      },
    );
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('request failures throw OpenQuickError with status code details and retry metadata', async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () =>
    new Response(JSON.stringify({ error: 'rate limited', code: 'rate_limited', scope: 'ai:rpm', reset: '2026-01-01T00:01:00Z' }), {
      status: 429,
      headers: { 'content-type': 'application/json', 'retry-after': '30' },
    });
  try {
    await assert.rejects(
      () => quick.db.collection('notes').list(),
      (error) => {
        assert.equal(error instanceof OpenQuickError, true);
        assert.equal(error.status, 429);
        assert.equal(error.code, 'rate_limited');
        assert.equal(error.retryAfter, 30);
        assert.equal(error.details.scope, 'ai:rpm');
        return true;
      },
    );
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('uploads.put reports progress and passes abort signal', async () => {
  const originalFetch = globalThis.fetch;
  const controller = new AbortController();
  const progress = [];
  let sawSignal = false;
  let sawRawUpload = false;
  globalThis.fetch = async (path, init = {}) => {
    sawSignal = init.signal === controller.signal;
    sawRawUpload = String(path).includes('/uploads?name=progress.txt');
    const reader = init.body?.getReader?.();
    if (reader) {
      while (!(await reader.read()).done) {}
    }
    return jsonResponse({ id: 'upload-progress', url: '/_quick/uploads/upload-progress', name: 'progress.txt' });
  };
  try {
    const blob = new Blob(['hello'], { type: 'text/plain' });
    const upload = await quick.uploads.put(blob, { name: 'progress.txt', signal: controller.signal, onProgress: (p) => progress.push(p) });
    assert.equal(upload.id, 'upload-progress');
    assert.equal(sawSignal, true);
    assert.equal(sawRawUpload, true);
    assert.equal(progress.length > 0, true);
    assert.equal(progress.at(-1).loaded, 5);
    assert.equal(progress.at(-1).total, 5);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('SDK request methods pass AbortSignal through fetch', async () => {
  const originalFetch = globalThis.fetch;
  const controller = new AbortController();
  const seen = [];
  globalThis.fetch = async (_path, init = {}) => {
    seen.push(init.signal);
    return jsonResponse({ documents: [] });
  };
  try {
    await quick.db.collection('notes').list({ signal: controller.signal });
    assert.equal(seen[0], controller.signal);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('SDK uses path-fallback API routes when the page is under /~/site', async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = Object.getOwnPropertyDescriptor(globalThis, 'location');
  Object.defineProperty(globalThis, 'location', {
    configurable: true,
    value: {
      href: 'https://quick.example.com/~/demo/app/',
      pathname: '/~/demo/app/',
      protocol: 'https:',
      host: 'quick.example.com',
    },
  });

  const seen = [];
  globalThis.fetch = async (path) => {
    seen.push(String(path));
    if (String(path).includes('/uploads/')) {
      return jsonResponse({ id: 'upload-1', url: '/_quick/uploads/upload-1' });
    }
    if (String(path).endsWith('/capabilities')) {
      return jsonResponse({ ai: true });
    }
    if (String(path).endsWith('/ai/images')) {
      return jsonResponse({ id: 'image-1', url: '/_quick/uploads/image-1' });
    }
    return jsonResponse({ authenticated: false, provider: 'anonymous', subject: 'anonymous' });
  };

  try {
    const { quick: scopedQuick } = await import(`../dist/quick.js?path-fallback=${Date.now()}`);
    await scopedQuick.identity.current();
    const upload = await scopedQuick.uploads.get('upload-1');
    const image = await scopedQuick.ai.image('draw a badge');

    assert.equal(seen[0], '/~/demo/_quick/identity');
    assert.equal(seen[1], '/~/demo/_quick/uploads/upload-1');
    assert.equal(seen[2], '/~/demo/_quick/capabilities');
    assert.equal(seen[3], '/~/demo/_quick/ai/images');
    assert.equal(upload.url, '/~/demo/_quick/uploads/upload-1');
    assert.equal(image.url, '/~/demo/_quick/uploads/image-1');
  } finally {
    globalThis.fetch = originalFetch;
    if (originalLocation) {
      Object.defineProperty(globalThis, 'location', originalLocation);
    } else {
      delete globalThis.location;
    }
  }
});

test('SDK falls back to root API routes when location pathname is unavailable', async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = Object.getOwnPropertyDescriptor(globalThis, 'location');
  Object.defineProperty(globalThis, 'location', {
    configurable: true,
    value: {
      href: 'https://quick.example.com/app/',
      protocol: 'https:',
      host: 'quick.example.com',
    },
  });

  const seen = [];
  globalThis.fetch = async (path) => {
    seen.push(String(path));
    return jsonResponse({ authenticated: false, provider: 'anonymous', subject: 'anonymous' });
  };

  try {
    const { quick: scopedQuick } = await import(`../dist/quick.js?missing-pathname=${Date.now()}`);
    await scopedQuick.identity.current();
    assert.equal(seen[0], '/_quick/identity');
  } finally {
    globalThis.fetch = originalFetch;
    if (originalLocation) {
      Object.defineProperty(globalThis, 'location', originalLocation);
    } else {
      delete globalThis.location;
    }
  }
});

test('SDK uses path-fallback WebSocket routes for realtime', async () => {
  const originalLocation = Object.getOwnPropertyDescriptor(globalThis, 'location');
  const originalWebSocket = globalThis.WebSocket;
  Object.defineProperty(globalThis, 'location', {
    configurable: true,
    value: {
      href: 'https://quick.example.com/~/demo/app/',
      pathname: '/~/demo/app/',
      protocol: 'https:',
      host: 'quick.example.com',
    },
  });

  const seen = [];
  class FakeWebSocket {
    static OPEN = 1;
    static CLOSED = 3;

    constructor(url) {
      this.url = url;
      this.readyState = 0;
      seen.push(String(url));
    }

    addEventListener() {}
    removeEventListener() {}
    send() {}
  }
  globalThis.WebSocket = FakeWebSocket;

  try {
    const { quick: scopedQuick } = await import(`../dist/quick.js?realtime-path-fallback=${Date.now()}`);
    scopedQuick.realtime.channel('room').send('cursor', { x: 1 });
    assert.equal(seen[0], 'wss://quick.example.com/~/demo/_quick/realtime');
  } finally {
    globalThis.WebSocket = originalWebSocket;
    if (originalLocation) {
      Object.defineProperty(globalThis, 'location', originalLocation);
    } else {
      delete globalThis.location;
    }
  }
});

test('SDK realtime reconnects with backoff and resubscribes without duplicate handlers', async () => {
  const originalLocation = Object.getOwnPropertyDescriptor(globalThis, 'location');
  const originalWebSocket = globalThis.WebSocket;
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  Object.defineProperty(globalThis, 'location', {
    configurable: true,
    value: {
      href: 'https://demo.quick.example.com/',
      pathname: '/',
      protocol: 'https:',
      host: 'demo.quick.example.com',
    },
  });

  const sockets = [];
  let scheduled = null;
  let scheduledDelay = null;
  class FakeWebSocket {
    static OPEN = 1;
    static CLOSED = 3;

    constructor(url) {
      this.url = url;
      this.readyState = 0;
      this.sent = [];
      this.listeners = new Map();
      sockets.push(this);
    }

    addEventListener(event, cb) {
      const listeners = this.listeners.get(event) || new Set();
      listeners.add(cb);
      this.listeners.set(event, listeners);
    }

    removeEventListener(event, cb) {
      this.listeners.get(event)?.delete(cb);
    }

    send(value) {
      this.sent.push(String(value));
    }

    dispatch(event, payload = {}) {
      for (const cb of Array.from(this.listeners.get(event) || [])) {
        cb(payload);
      }
    }

    open() {
      this.readyState = FakeWebSocket.OPEN;
      this.dispatch('open');
    }

    closeAbnormally() {
      this.readyState = FakeWebSocket.CLOSED;
      this.dispatch('close', { code: 1006 });
    }

    message(data) {
      this.dispatch('message', { data });
    }
  }

  globalThis.WebSocket = FakeWebSocket;
  globalThis.setTimeout = (cb, delay) => {
    scheduled = cb;
    scheduledDelay = delay;
    return 1;
  };
  globalThis.clearTimeout = () => {};

  try {
    const { quick: scopedQuick } = await import(`../dist/quick.js?reconnect=${Date.now()}`);
    const states = [];
    const received = [];
    scopedQuick.realtime.onStateChange((state) => states.push(state));
    scopedQuick.realtime.channel('room').on('update', (data) => received.push(data));

    assert.equal(sockets.length, 1);
    sockets[0].open();
    assert.equal(sockets[0].sent.filter((msg) => msg.includes('"type":"subscribe"')).length, 1);

    sockets[0].closeAbnormally();
    assert.equal(typeof scheduled, 'function');
    assert.equal(scheduledDelay >= 1000, true);
    scheduled();
    assert.equal(sockets.length, 2);
    sockets[1].open();
    assert.equal(sockets[1].sent.filter((msg) => msg.includes('"type":"subscribe"')).length, 1);

    sockets[1].message(JSON.stringify({ channel: 'room', event: 'update', data: { n: 1 } }));
    assert.equal(received.length, 1);
    assert.deepEqual(received[0], { n: 1 });
    assert.equal(states.includes('connected'), true);
    assert.equal(states.includes('disconnected'), true);
    assert.equal(states.includes('reconnecting'), true);
  } finally {
    globalThis.WebSocket = originalWebSocket;
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
    if (originalLocation) {
      Object.defineProperty(globalThis, 'location', originalLocation);
    } else {
      delete globalThis.location;
    }
  }
});
