# Pi Artifact extension for OpenQuick

This Pi extension registers an `artifact` tool. The tool writes artifact files to `.pi/openquick-artifacts/<site>/` and publishes them with:

```bash
quick deploy <artifact-dir> --site <site> --profile cf --no-build --yes --json
```

The resulting artifact is served by the Cloudflare Access-protected OpenQuick host at:

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

If a static artifact needs the OpenQuick browser SDK, pass `sdk: true`. The extension injects a small bridge that imports `/_quick/sdk.js`, exposes `window.quick`, and dispatches `openquick:sdk-ready`:

```json
{
  "title": "SDK demo",
  "sdk": true,
  "html": "<!doctype html><pre id=\"out\"></pre><script>addEventListener('openquick:sdk-ready', async (event) => { out.textContent = JSON.stringify(await event.detail.quick.identity.current(), null, 2); });</script>"
}
```

### Code Mode artifact

Pass `mode: "codemode"` for a pi.dev-artifact-style code playground. The deployed page is an in-browser file editor + live preview that:

- imports the OpenQuick SDK from `/_quick/sdk.js`,
- exposes `window.quick` inside the preview iframe,
- shows the authenticated Cloudflare/OpenQuick identity,
- persists edited files in `quick.db.collection('codemode_files')`, and
- lets the user save with the button or `Cmd/Ctrl+S`.

With no files, it creates a starter `index.html`, `style.css`, and `script.js`:

```json
{
  "title": "Interactive demo",
  "mode": "codemode"
}
```

With files, those files become the editable starter bundle inside Code Mode:

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

If `site` is omitted, the extension derives a deterministic slug from the title and content hash. Absolute paths, `..`, `.env*`, `.git`, `.quick`, `.ssh`, and `node_modules` are rejected before writing files.

Because deployment uses `--yes`, an explicit existing `site` slug can overwrite that OpenQuick site non-interactively.
