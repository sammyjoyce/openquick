import test from 'node:test';
import assert from 'node:assert/strict';
import { quick } from '../dist/quick.js';

test('OpenQuick SDK surface shape', () => {
  assert.equal(typeof quick, 'object');
  assert.equal(typeof quick.identity.current, 'function');
  assert.equal(typeof quick.identity.onChange, 'function');
  assert.equal(typeof quick.db.collection, 'function');
  assert.equal(typeof quick.uploads.put, 'function');
  assert.equal(typeof quick.uploads.get, 'function');
  assert.equal(typeof quick.uploads.remove, 'function');
  assert.equal(typeof quick.realtime.channel, 'function');
  assert.equal(typeof quick.capabilities, 'function');
  assert.equal(typeof quick.ai.chat, 'function');
  assert.equal(typeof quick.ai.image, 'function');
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
  globalThis.fetch = async () =>
    jsonResponse({
      documents: [
        {
          id: 'doc-1',
          data: { text: 'hello', by: 'sam@example.com' },
          created_by: 'dev:sam@example.com',
          created_at: '2026-01-01T00:00:00Z',
          updated_at: '2026-01-01T00:00:00Z',
        },
      ],
    });
  try {
    const docs = await quick.db.collection('notes').list();
    assert.equal(docs.length, 1);
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
