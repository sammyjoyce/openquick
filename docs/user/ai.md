# AI runtime

OpenQuick AI is disabled by default. Hosts only advertise `capabilities().ai === true` after an operator enables AI providers and access policy in the host config.

## Host configuration

A typical host config keeps provider credentials on the server and exposes only allowlisted models to sites:

```json
{
  "ai": {
    "enabled": true,
    "providers": [
      {
        "name": "openai",
        "api_key_env": "OPENAI_API_KEY",
        "models": ["gpt-4o-mini"],
        "image_models": ["gpt-image-1"]
      }
    ],
    "allow_identities": ["alice@example.com", "group:engineering"],
    "budgets": {
      "per_identity_per_day_usd": 2.0,
      "per_site_per_day_usd": 20.0,
      "max_prompt_tokens": 8000,
      "max_completion_tokens": 2000
    }
  }
}
```

Recommended policy:

- `enabled` defaults to `false`.
- Provider API keys are read from environment variables or a server-side secret store, never from site files.
- `allow_identities` should be narrow; use groups only when the identity provider supplies trusted groups.
- Budgets and token limits should be set before enabling the feature for shared hosts.
- Sites should check `await quick.capabilities()` before rendering AI controls.

## SDK usage

Import the same-origin SDK:

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';
</script>
```

Chat calls use `POST /_quick/ai/chat` behind the SDK:

```js
const response = await quick.ai.chat([
  { role: 'system', content: 'Answer with one short paragraph.' },
  { role: 'user', content: 'What changed in this release?' },
], {
  model: 'gpt-4o-mini',
});

console.log(response.message.content);
```

Streaming chat sends `stream: true`; the SDK consumes server-sent events, calls `onDelta` for each text delta, and resolves with the assembled final response:

```js
let text = '';
const final = await quick.ai.chat(
  [{ role: 'user', content: 'Draft a launch note.' }],
  {
    stream: true,
    onDelta(delta) {
      text += delta;
      output.textContent = text;
    },
  },
);
```

Image generation uses `POST /_quick/ai/images` and returns an internal upload URL:

```js
const image = await quick.ai.image('A simple OpenQuick rocket icon', {
  model: 'gpt-image-1',
  size: '1024x1024',
});

preview.src = image.url;
```

If the host does not advertise AI, SDK calls fail early with errors such as `quick.ai.chat is not available on this host`.

## Security notes

- Keys and provider routing are server-side only; never commit provider tokens to `quick.json`, source files, or `.env` files that are deployed.
- Browser calls are same-origin `/_quick/*` requests. Do not proxy AI requests to arbitrary origins from site JavaScript.
- Treat prompts, uploaded context, and model output as user data. Avoid sending secrets, raw access tokens, or private customer records unless the host policy explicitly allows it.
- Use identity allowlists, quotas, and audit logs together. Budgets limit blast radius but do not replace authorization.
- Render model output safely. Escape HTML unless the application has a trusted sanitizer and a reason to allow markup.
