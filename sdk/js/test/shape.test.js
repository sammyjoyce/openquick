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
