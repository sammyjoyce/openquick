# Pi Artifact extension for OpenQuick

This Pi extension registers an `artifact` tool. The tool writes artifact files to
`.pi/artifacts/<site>/` and publishes them with:

```bash
quick deploy <artifact-dir> --site <site> --profile cf --no-build --json
```

Pass `overwrite: true` to intentionally append `--yes` for an existing site.

The resulting artifact is served by the Cloudflare Access-protected OpenQuick
host at:

```text
https://<site>.sammy.sh
```

## Load it

For one-off testing from this repository:

```bash
pi -e ./extensions/pi-artifact/index.ts
```

For project-local auto-discovery, this checkout also has a shim at:

```text
.pi/extensions/openquick-artifact/index.ts
```

## Tool shape

### Static artifact

The tool accepts either a single `html` string or a `files` array:

```json
{
  "title": "Demo",
  "html": "<!doctype html><h1>Hello</h1>"
}
```

or:

```json
{
  "site": "my-artifact",
  "files": [
    { "path": "index.html", "content": "<!doctype html><script src=\"/assets/app.js\"></script>" },
    { "path": "assets/app.js", "content": "console.log('hello')" }
  ]
}
```

If a static artifact needs the OpenQuick browser SDK, pass `sdk: true`. The
extension injects a small bridge that imports `/_quick/sdk.js`, exposes
`window.quick`, and dispatches `openquick:sdk-ready`:

```json
{
  "title": "SDK demo",
  "sdk": true,
  "html": "<!doctype html><pre id=\"out\"></pre><script>addEventListener('openquick:sdk-ready', async (event) => { out.textContent = JSON.stringify(await event.detail.quick.identity.current(), null, 2); });</script>"
}
```

By default, the tool does not pass `--yes`. If you provide an explicit `site`
that already exists, OpenQuick's normal overwrite guard applies. Pass
`"overwrite": true` only when you intentionally want to replace or update that
site.

```json
{
  "site": "my-artifact",
  "overwrite": true,
  "html": "<!doctype html><h1>Replace my-artifact</h1>"
}
```

### Code Mode artifact

Pass `mode: "codemode"` for a pi.dev-artifact-style code playground. The deployed
page is an in-browser file editor + live preview that:

- imports the OpenQuick SDK from `/_quick/sdk.js` in the editor shell,
- keeps SDK credentials out of the sandboxed preview iframe,
- shows the authenticated Cloudflare/OpenQuick identity in the editor chrome,
- persists edited files in `quick.db.collection('codemode_files')`, and
- lets the user save with the button or `Cmd/Ctrl+S`.

With no files, it creates a starter `index.html`, `style.css`, and
`script.js`:

```json
{
  "title": "Interactive demo",
  "mode": "codemode"
}
```

With files, those files become the editable starter bundle inside Code Mode. The
preview runs with `sandbox="allow-scripts"` and receives only a non-credential
`window.quick` stub; use static mode with `sdk: true` when the final artifact
itself should call OpenQuick APIs.

```json
{
  "site": "my-code-artifact",
  "mode": "codemode",
  "files": [
    { "path": "index.html", "content": "<!doctype html><h1 id=\"app\"></h1><script type=\"module\" src=\"script.js\"></script>" },
    { "path": "script.js", "content": "app.textContent = 'Hello Code Mode';" }
  ]
}
```

## Slugs and safety

If `site` is omitted, the extension derives a deterministic slug from the title
and content hash. Absolute paths, `..`, and any path segment named `.git`,
`.quick`, `.ssh`, or `node_modules`, or starting with `.env`, are rejected before
writing files.

The extension omits `--yes` unless `overwrite: true` is set, so explicit existing
`site` slugs keep OpenQuick's overwrite guard instead of being overwritten
non-interactively. Generated slugs for new artifacts deploy without requiring the
overwrite opt-in.
