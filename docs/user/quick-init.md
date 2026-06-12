# `quick init`

`quick init` creates a static OpenQuick site that can be edited by a human or an agent and deployed with `quick deploy`.

## Common commands

```bash
quick init                         # initialize the current directory
quick init lunch-vote              # create ./lunch-vote
quick init --template blank
quick init --template realtime lunch-vote
quick init --name lunch-vote --profile lab
```

The site name defaults to the directory name normalized as a DNS label: lowercase ASCII, `a-z0-9-`, no leading or trailing hyphen, and at most 63 characters.

## Generated site shape

A normal scaffold contains:

```text
index.html
quick.json
AGENTS.md
docs/openquick-api.md
.quickignore
```

The SDK import should stay same-origin:

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';
</script>
```

## `quick.json`

Minimal static site:

```json
{
  "$schema": "https://openquick.dev/schemas/site.v1.json",
  "name": "lunch-vote",
  "source": ".",
  "output": ".",
  "build": null,
  "profile": "lab",
  "subdomain": "lunch-vote",
  "sdk": {
    "enabled": true,
    "import": "/_quick/sdk.js"
  }
}
```

Build-output site:

```json
{
  "$schema": "https://openquick.dev/schemas/site.v1.json",
  "name": "planning-board",
  "source": ".",
  "output": "dist",
  "build": "bun run build",
  "profile": "lab",
  "subdomain": "planning-board",
  "routing": {
    "spa_fallback": "/index.html"
  }
}
```

Field notes:

- `name`: host catalog name and default deploy target.
- `source`: directory where the build command runs.
- `output`: directory mirrored to the host; deploys must produce static files here.
- `build`: optional command; `null` means no build step.
- `profile`: user-global host profile to deploy to.
- `subdomain`: hostname label under the profile base domain.
- `sdk.enabled`: whether generated code should use OpenQuick runtime APIs.
- `sdk.import`: always same-origin, normally `/_quick/sdk.js`.
- `routing.spa_fallback`: optional SPA fallback path.

Do not put secrets in `quick.json`; it is expected to be committed.

## Ignore rules

`.quickignore` uses gitignore-style patterns before rsync. Defaults should include:

```gitignore
.git/
.quick/
node_modules/
.DS_Store
.env
.env.*
```

`.quick/` is local state only. It may cache deployments and host capabilities, but it is not the source of truth.
