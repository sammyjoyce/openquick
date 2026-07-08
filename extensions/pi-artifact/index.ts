import { createHash } from "node:crypto";
import { mkdir, realpath, rm, writeFile } from "node:fs/promises";
import { dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { StringEnum, Type, type Static } from "@earendil-works/pi-ai";
import {
	CONFIG_DIR_NAME,
	DEFAULT_MAX_BYTES,
	DEFAULT_MAX_LINES,
	defineTool,
	formatSize,
	truncateTail,
	type ExtensionAPI,
	type Theme,
	type TruncationResult,
	withFileMutationQueue,
} from "@earendil-works/pi-coding-agent";
import { truncateToWidth, type Component } from "@earendil-works/pi-tui";

const PROFILE = "cf";
const ARTIFACT_ROOT = join(CONFIG_DIR_NAME, "openquick-artifacts");

const ArtifactParamsSchema = Type.Object(
	{
		title: Type.Optional(Type.String({ description: "Optional string. Human-readable title. If `site` is omitted, it contributes to the generated site slug; in `mode: \"codemode\"`, it also labels the generated editor page. If omitted, the generated slug base defaults to `codemode` for Code Mode and `artifact` otherwise; the generated Code Mode page title falls back to an explicit `site`, then `OpenQuick Code Mode`." })),
		site: Type.Optional(Type.String({ description: "Optional OpenQuick site slug string. Must contain only lowercase letters, digits, and hyphens, start and end with a letter or digit, and be at most 63 characters. If omitted, the tool generates deterministic `artifact-<base>-<10-hex-content-hash>` where `<base>` is the slugified `title` or mode default (`codemode`/`artifact`), clipped to 34 characters with trailing hyphens removed; the final slug is validated. The deployed or planned URL is `https://<site>.sammy.sh`. For an explicit existing site, set `overwrite: true` only when intentionally updating it." })),
		html: Type.Optional(Type.String({ description: "Optional UTF-8 HTML string shortcut for `index.html`. In static mode, either `html` or `files` must provide an `index.html`; `html` is mapped first, so a later `files` entry whose normalized path is `index.html` overwrites it. In Code Mode, this becomes starter `index.html` content embedded inside the generated editor instead of a directly published file." })),
		files: Type.Optional(Type.Array(
			Type.Object(
				{
					path: Type.String({ description: "Required string path relative to the artifact root or Code Mode starter bundle, such as `index.html` or `assets/app.js`. Backslashes are normalized to `/` and one leading `./` is stripped. No globbing, regex, shell expansion, absolute paths, empty paths, NUL bytes, empty segments, `.` segments, or `..` segments. Path segments named `.git`, `.quick`, `.ssh`, `node_modules`, or starting with `.env` are rejected; resolved static output paths must stay inside the artifact directory." }),
					content: Type.String({ description: "Required string. Written as UTF-8 text in static mode, or embedded as starter-file text in the Code Mode editor. Binary upload/object content is not supported by this parameter." }),
				},
				{ additionalProperties: false },
			),
			{ description: "Optional array of text files with `{ path, content }`. In static mode, files are written under `.pi/openquick-artifacts/<site>` and must include `index.html` unless `html` is provided. In Code Mode, entries become editable starter files embedded in the generated editor; missing `index.html`, `style.css`, and `script.js` are defaulted. Duplicate normalized paths are resolved by the later entry winning." },
		)),
		mode: Type.Optional(StringEnum(["static", "codemode"] as const, { description: "Optional enum string: `\"static\"` or `\"codemode\"`. Defaults to `\"static\"`. `static` publishes the provided files directly and requires an `index.html`. `codemode` publishes exactly one generated `index.html` containing an SDK-backed editor shell, sandboxed live preview, and `quick.db.collection('codemode_files')` persistence; supplied `html`/`files` are starter content, not separately published files. Use static mode with `sdk: true` when the final page itself should call OpenQuick APIs." })),
		sdk: Type.Optional(Type.Boolean({ description: "Optional boolean. Defaults to `false` in static mode. In static mode, `true` injects a bridge into `index.html` unless the HTML already contains `/_quick/sdk.js` or `openquick:sdk-ready`; the bridge imports `/_quick/sdk.js`, sets `window.quick`, and dispatches `openquick:sdk-ready`. In Code Mode, this parameter has no effect on generation because the editor shell always imports the SDK and returned details report `sdk: true`." })),
		dryRun: Type.Optional(Type.Boolean({ description: "Optional boolean, default `false`. When `true`, append `--dry-run` to `quick deploy` and return the planned URL if the command succeeds. The tool still deletes/recreates the local artifact directory, writes files, and runs `quick deploy`; only remote publishing is dry-run." })),
		overwrite: Type.Optional(Type.Boolean({ description: "Optional boolean, default `false`. When `true`, append `--yes` to `quick deploy` to intentionally update or replace an existing site. Leave `false` unless the user explicitly wants to overwrite; with an explicit existing `site`, OpenQuick's overwrite guard can fail and the error includes a hint to retry with `overwrite: true`." })),
	},
	{ additionalProperties: false },
);

type ArtifactParams = Static<typeof ArtifactParamsSchema>;
type ArtifactFileParam = NonNullable<ArtifactParams["files"]>[number];

type DeployJson = {
	format_version?: string;
	site?: string;
	profile?: string;
	release?: string;
	url?: string;
	changed?: number;
	reused?: number;
	deleted?: number;
	dry_run?: boolean;
	summary?: {
		added_count?: number;
		changed_count?: number;
		deleted_count?: number;
		excluded_count?: number;
	};
};

type OutputTruncationDetails = {
	wasTruncated: boolean;
	originalSize: string;
	originalBytes: number;
	originalLines: number;
	outputSize: string;
	outputBytes: number;
	outputLines: number;
	truncatedBy: TruncationResult["truncatedBy"];
	maxLines: number;
	maxSize: string;
	maxBytes: number;
};

type ArtifactDetails = {
	site: string;
	profile: string;
	url?: string;
	release?: string;
	mode: "static" | "codemode";
	sdk: boolean;
	dryRun: boolean;
	artifactDir: string;
	files: string[];
	command: string[];
	stdout: string;
	stderr: string;
	stdoutTruncated: boolean;
	stderrTruncated: boolean;
	stdoutTruncation: OutputTruncationDetails;
	stderrTruncation: OutputTruncationDetails;
	deploy?: DeployJson;
};

function slugify(input: string): string {
	const slug = input
		.toLowerCase()
		.normalize("NFKD")
		.replace(/[\u0300-\u036f]/g, "")
		.replace(/[^a-z0-9]+/g, "-")
		.replace(/^-+|-+$/g, "")
		.replace(/-{2,}/g, "-");
	return slug || "artifact";
}

function escapeHtml(input: string): string {
	return input
		.replace(/&/g, "&amp;")
		.replace(/</g, "&lt;")
		.replace(/>/g, "&gt;")
		.replace(/"/g, "&quot;")
		.replace(/'/g, "&#39;");
}

function validateSiteSlug(site: string): string {
	if (!/^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(site)) {
		throw new Error(`Invalid OpenQuick site slug ${JSON.stringify(site)}. Use lowercase a-z, digits, and hyphens; no edge hyphens; max 63 chars.`);
	}
	return site;
}

function normalizeArtifactPath(path: string): string {
	const withoutArtifactSigil = path.startsWith("@") ? path.slice(1) : path;
	const cleaned = withoutArtifactSigil.replace(/\\/g, "/").replace(/^\.\//, "");
	if (!cleaned || cleaned.startsWith("/") || cleaned.includes("\0")) {
		throw new Error(`Invalid artifact file path ${JSON.stringify(path)}.`);
	}
	const parts = cleaned.split("/");
	if (parts.some((part) => part === "" || part === "." || part === "..")) {
		throw new Error(`Artifact file path must be relative and must not contain empty, '.', or '..' segments: ${JSON.stringify(path)}.`);
	}
	if (parts.some((part) => [".git", ".quick", ".ssh", "node_modules"].includes(part) || part.startsWith(".env"))) {
		throw new Error(`Refusing to publish reserved or secret-looking path segment in ${JSON.stringify(path)}.`);
	}
	return parts.join("/");
}

function sdkBridgeScript(): string {
	return `<script type="module">
import { quick } from '/_quick/sdk.js';
window.quick = quick;
window.dispatchEvent(new CustomEvent('openquick:sdk-ready', { detail: { quick } }));
</script>`;
}

function injectSdkBridge(html: string): string {
	if (html.includes("/_quick/sdk.js") || html.includes("openquick:sdk-ready")) return html;
	const bridge = sdkBridgeScript();
	if (/<\/head\s*>/i.test(html)) {
		return html.replace(/<\/head\s*>/i, `${bridge}\n</head>`);
	}
	if (/<\/body\s*>/i.test(html)) {
		return html.replace(/<\/body\s*>/i, `${bridge}\n</body>`);
	}
	return `${bridge}\n${html}`;
}

function scriptSafeJson(value: unknown): string {
	return JSON.stringify(value).replace(/<\//g, "<\\/");
}

function initialCodeFilesFromParams(params: ArtifactParams): ArtifactFileParam[] {
	const provided = new Map<string, string>();
	if (params.html !== undefined) provided.set("index.html", params.html);
	for (const file of params.files ?? []) {
		provided.set(normalizeArtifactPath(file.path), file.content);
	}
	if (!provided.has("index.html")) {
		provided.set(
			"index.html",
			`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OpenQuick Code Mode</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <main>
    <p class="eyebrow">OpenQuick artifact</p>
    <h1>Hello from Code Mode</h1>
    <p>Edit the files on the left. The editor shell uses the OpenQuick SDK, while the preview runs in an isolated sandbox.</p>
    <pre id="identity">Loading preview bridge…</pre>
  </main>
  <script type="module" src="script.js"></script>
</body>
</html>`,
		);
	}
	if (!provided.has("style.css")) {
		provided.set(
			"style.css",
			`body { margin: 0; min-height: 100vh; display: grid; place-items: center; color: #e5e7eb; background: radial-gradient(circle at top, #1d4ed8, #020617 65%); font-family: ui-sans-serif, system-ui, sans-serif; }
main { width: min(760px, calc(100% - 48px)); padding: 42px; border: 1px solid rgba(255,255,255,.16); border-radius: 28px; background: rgba(15,23,42,.78); box-shadow: 0 24px 90px rgba(0,0,0,.45); }
.eyebrow { color: #93c5fd; text-transform: uppercase; letter-spacing: .18em; font-size: .78rem; font-weight: 800; }
h1 { margin: 0 0 16px; font-size: clamp(2.4rem, 7vw, 5rem); letter-spacing: -.06em; }
p { color: #cbd5e1; line-height: 1.7; }
code, pre { color: #f8fafc; }
pre { white-space: pre-wrap; padding: 16px; border-radius: 16px; background: rgba(2,6,23,.55); }`,
		);
	}
	if (!provided.has("script.js")) {
		provided.set(
			"script.js",
			`const identityEl = document.querySelector('#identity');
const quick = window.quick;
try {
  const me = quick ? await quick.identity.current() : null;
  identityEl.textContent = me ? JSON.stringify(me, null, 2) : 'Preview bridge not found.';
} catch (error) {
  identityEl.textContent = error instanceof Error ? error.message : String(error);
}`,
		);
	}
	return [...provided.entries()].map(([path, content]) => ({ path, content }));
}

function codemodeHtml(params: ArtifactParams, codeFiles: ArtifactFileParam[]): string {
	const title = params.title ?? params.site ?? "OpenQuick Code Mode";
	const initial = scriptSafeJson(codeFiles);
	return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${escapeHtml(title)}</title>
  <style>
    :root { color-scheme: dark; --bg: #020617; --panel: #0f172a; --line: rgba(148,163,184,.24); --text: #e2e8f0; --muted: #94a3b8; --accent: #38bdf8; --ok: #22c55e; --warn: #f59e0b; }
    * { box-sizing: border-box; }
    body { margin: 0; height: 100vh; overflow: hidden; color: var(--text); background: var(--bg); font: 14px/1.45 ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
    header { height: 58px; display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 0 18px; border-bottom: 1px solid var(--line); background: rgba(15,23,42,.96); }
    header strong { letter-spacing: -.03em; font-size: 16px; }
    header .meta { color: var(--muted); font-size: 12px; }
    button { border: 1px solid var(--line); border-radius: 10px; padding: 8px 11px; color: var(--text); background: #111827; cursor: pointer; }
    button.primary { border-color: rgba(56,189,248,.5); background: rgba(14,116,144,.35); }
    button:hover { border-color: rgba(226,232,240,.45); }
    .shell { height: calc(100vh - 58px); display: grid; grid-template-columns: 220px minmax(320px, 1fr) minmax(320px, 1fr); }
    aside { border-right: 1px solid var(--line); background: #020617; overflow: auto; }
    .file { width: 100%; display: block; border: 0; border-bottom: 1px solid rgba(148,163,184,.12); border-radius: 0; padding: 12px 14px; text-align: left; color: var(--muted); background: transparent; }
    .file.active { color: var(--text); background: rgba(56,189,248,.12); box-shadow: inset 3px 0 0 var(--accent); }
    .editor, .preview { min-width: 0; min-height: 0; display: flex; flex-direction: column; }
    .editor { border-right: 1px solid var(--line); }
    .bar { min-height: 42px; display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 0 12px; border-bottom: 1px solid var(--line); color: var(--muted); background: rgba(15,23,42,.72); }
    textarea { flex: 1; width: 100%; border: 0; outline: 0; resize: none; padding: 18px; color: #dbeafe; background: #030712; font: 13px/1.55 ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; tab-size: 2; }
    iframe { flex: 1; width: 100%; border: 0; background: white; }
    #status { color: var(--muted); }
    #status.ok { color: var(--ok); }
    #status.warn { color: var(--warn); }
    @media (max-width: 980px) { .shell { grid-template-columns: 180px 1fr; } .preview { display: none; } }
  </style>
</head>
<body>
  <header>
    <div><strong>${escapeHtml(title)}</strong><div class="meta">OpenQuick Code Mode · SDK-backed editor · sandboxed preview</div></div>
    <div><span id="status">Loading…</span> <button id="save">Save to quick.db</button> <button class="primary" id="run">Run preview</button></div>
  </header>
  <div class="shell">
    <aside id="files"></aside>
    <section class="editor"><div class="bar"><span id="current"></span><span>SDK credentials stay in the editor shell</span></div><textarea id="code" spellcheck="false"></textarea></section>
    <section class="preview"><div class="bar"><span>Sandboxed preview</span><span id="who"></span></div><iframe id="preview" sandbox="allow-scripts"></iframe></section>
  </div>
  <script type="module">
    import { quick } from '/_quick/sdk.js';
    window.quick = quick;
    const initialFiles = ${initial};
    const files = new Map(initialFiles.map((file) => [file.path, file.content]));
    const status = document.querySelector('#status');
    const fileList = document.querySelector('#files');
    const code = document.querySelector('#code');
    const current = document.querySelector('#current');
    const preview = document.querySelector('#preview');
    const who = document.querySelector('#who');
    const docs = quick.db.collection('codemode_files');
    let active = files.has('index.html') ? 'index.html' : initialFiles[0].path;

    const setStatus = (message, kind = '') => { status.textContent = message; status.className = kind; };

    function renderFileList() {
      fileList.innerHTML = '';
      for (const path of [...files.keys()].sort()) {
        const button = document.createElement('button');
        button.className = 'file' + (path === active ? ' active' : '');
        button.textContent = path;
        button.onclick = () => { saveEditor(); active = path; loadEditor(); renderFileList(); };
        fileList.append(button);
      }
    }

    function saveEditor() { if (active) files.set(active, code.value); }
    function loadEditor() { current.textContent = active; code.value = files.get(active) || ''; }
    function previewBridgeScript() {
      return '<script>window.quick = Object.freeze({ identity: { current: async () => ({ authenticated: false, provider: "preview-sandbox", note: "OpenQuick SDK credentials are available only to the parent Code Mode editor." }) }, db: { collection: () => { throw new Error("quick.db is disabled inside the sandboxed preview. Use the Code Mode save button to persist files."); } } }); window.dispatchEvent(new CustomEvent("openquick:sdk-ready", { detail: { quick: window.quick, sandboxed: true } }));<' + '/script>';
    }
    function renderPreview() {
      saveEditor();
      let html = files.get('index.html') || '<!doctype html><h1>No index.html</h1>';
      if (!html.includes('openquick:sdk-ready')) {
        if (/<\\/head\\s*>/i.test(html)) html = html.replace(/<\\/head\\s*>/i, previewBridgeScript() + '\\n</head>');
        else html = previewBridgeScript() + '\\n' + html;
      }
      const css = files.get('style.css');
      if (css && !html.includes('data-codemode-style')) {
        const style = '<style data-codemode-style>' + css + '</style>';
        html = /<\\/head\\s*>/i.test(html) ? html.replace(/<\\/head\\s*>/i, style + '\\n</head>') : style + html;
      }
      const js = files.get('script.js');
      if (js && !html.includes('data-codemode-script')) {
        const script = '<script type="module" data-codemode-script>' + String(js).replace(/<\\/script/gi, '<\\\\/script') + '<' + '/script>';
        html = /<\\/body\\s*>/i.test(html) ? html.replace(/<\\/body\\s*>/i, script + '\\n</body>') : html + script;
      }
      preview.srcdoc = html;
    }

    async function loadFromDb() {
      try {
        const records = await docs.list({ limit: 100 });
        for (const record of records) {
          if (typeof record.path === 'string' && typeof record.content === 'string') files.set(record.path, record.content);
        }
        setStatus(records.length ? 'Loaded saved code from quick.db' : 'Using bundled starter files', records.length ? 'ok' : '');
      } catch (error) {
        setStatus('quick.db load skipped: ' + (error instanceof Error ? error.message : String(error)), 'warn');
      }
    }

    async function saveToDb() {
      saveEditor();
      setStatus('Saving…');
      try {
        const existing = await docs.list({ limit: 100 });
        const existingIds = new Map(existing.filter((record) => typeof record.path === 'string' && record.id).map((record) => [record.path, record.id]));
        const updatedAt = new Date().toISOString();
        await Promise.all([...files].map(async ([path, content]) => {
          const patch = { path, content, updatedAt };
          const id = existingIds.get(path);
          if (id) await docs.update(id, patch);
          else await docs.create(patch);
        }));
        setStatus('Saved to quick.db', 'ok');
      } catch (error) {
        setStatus('Save failed: ' + (error instanceof Error ? error.message : String(error)), 'warn');
      }
    }

    try {
      const me = await quick.identity.current();
      who.textContent = me.email || me.login || 'authenticated';
    } catch {
      who.textContent = 'identity unavailable';
    }

    document.querySelector('#run').onclick = renderPreview;
    document.querySelector('#save').onclick = saveToDb;
    code.addEventListener('keydown', (event) => {
      if ((event.metaKey || event.ctrlKey) && event.key === 's') { event.preventDefault(); saveToDb(); }
    });
    await loadFromDb();
    renderFileList();
    loadEditor();
    renderPreview();
  </script>
</body>
</html>`;
}

function filesFromParams(params: ArtifactParams): ArtifactFileParam[] {
	if (params.mode === "codemode") {
		return [{ path: "index.html", content: codemodeHtml(params, initialCodeFilesFromParams(params)) }];
	}

	const byPath = new Map<string, string>();
	if (params.html !== undefined) {
		byPath.set("index.html", params.html);
	}
	for (const file of params.files ?? []) {
		const path = normalizeArtifactPath(file.path);
		byPath.set(path, file.content);
	}
	if (byPath.size === 0) {
		throw new Error("artifact requires either html or at least one file. Use mode='codemode' for an SDK-backed starter editor.");
	}
	if (!byPath.has("index.html")) {
		throw new Error("artifact must publish an index.html file. Provide html or a files entry with path='index.html'.");
	}
	return [...byPath.entries()].map(([path, content]) => ({
		path,
		content: params.sdk && path === "index.html" ? injectSdkBridge(content) : content,
	}));
}

function deriveSite(params: ArtifactParams, files: ArtifactFileParam[]): string {
	if (params.site) return validateSiteSlug(params.site);
	const title = slugify(params.title ?? (params.mode === "codemode" ? "codemode" : "artifact")).slice(0, 34).replace(/-+$/g, "") || "artifact";
	const sortedFiles = [...files].sort((a, b) => a.path.localeCompare(b.path));
	const hash = createHash("sha256")
		.update(JSON.stringify(sortedFiles.map((file) => [file.path, file.content])))
		.digest("hex")
		.slice(0, 10);
	return validateSiteSlug(`artifact-${title}-${hash}`.slice(0, 63).replace(/-+$/g, ""));
}

function isErrnoException(error: unknown): error is { code?: string } {
	return typeof error === "object" && error !== null && "code" in error;
}

function isPathWithin(root: string, target: string): boolean {
	const rel = relative(root, target);
	return rel === "" || (rel !== ".." && !rel.startsWith(`..${sep}`) && !isAbsolute(rel));
}

async function realpathNearestExistingAncestor(target: string): Promise<{ ancestor: string; realAncestor: string }> {
	let current = resolve(target);
	while (true) {
		try {
			return { ancestor: current, realAncestor: await realpath(current) };
		} catch (error) {
			if (!isErrnoException(error) || error.code !== "ENOENT") throw error;
			const parent = dirname(current);
			if (parent === current) throw error;
			current = parent;
		}
	}
}

async function resolveThroughNearestRealpath(target: string): Promise<string> {
	const resolvedTarget = resolve(target);
	const { ancestor, realAncestor } = await realpathNearestExistingAncestor(resolvedTarget);
	const remainder = relative(ancestor, resolvedTarget);
	return remainder === "" ? realAncestor : resolve(realAncestor, remainder);
}

async function assertArtifactDirSafe(workspaceRoot: string, artifactBase: string, artifactDir: string) {
	const resolvedWorkspaceRoot = await resolveThroughNearestRealpath(workspaceRoot);
	const resolvedArtifactBase = await resolveThroughNearestRealpath(artifactBase);
	const resolvedArtifactDir = await resolveThroughNearestRealpath(artifactDir);
	if (!isPathWithin(resolvedWorkspaceRoot, resolvedArtifactBase)) {
		throw new Error(`Refusing to use artifact base outside workspace: ${artifactBase}`);
	}
	if (!isPathWithin(resolvedWorkspaceRoot, resolvedArtifactDir)) {
		throw new Error(`Refusing to write artifact outside workspace: ${artifactDir}`);
	}
}

async function writeArtifactFiles(workspaceRoot: string, artifactBase: string, root: string, files: ArtifactFileParam[]) {
	await assertArtifactDirSafe(workspaceRoot, artifactBase, root);
	await rm(root, { recursive: true, force: true });
	await mkdir(root, { recursive: true });
	for (const file of files) {
		const target = resolve(root, file.path);
		const rel = relative(root, target);
		if (rel === ".." || rel.startsWith(`..${sep}`) || isAbsolute(rel)) {
			throw new Error(`Resolved artifact file escaped artifact root: ${file.path}`);
		}
		await mkdir(dirname(target), { recursive: true });
		await writeFile(target, file.content, "utf8");
	}
}

function parseDeployJson(stdout: string): DeployJson | undefined {
	const lines = stdout.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i];
		if (!line?.startsWith("{")) continue;
		try {
			return JSON.parse(line) as DeployJson;
		} catch {
			// Try earlier lines.
		}
	}
	return undefined;
}

function outputTruncationDetails(truncation: TruncationResult): OutputTruncationDetails {
	return {
		wasTruncated: truncation.truncated,
		originalSize: formatSize(truncation.totalBytes),
		originalBytes: truncation.totalBytes,
		originalLines: truncation.totalLines,
		outputSize: formatSize(truncation.outputBytes),
		outputBytes: truncation.outputBytes,
		outputLines: truncation.outputLines,
		truncatedBy: truncation.truncatedBy,
		maxLines: truncation.maxLines,
		maxSize: formatSize(truncation.maxBytes),
		maxBytes: truncation.maxBytes,
	};
}

function truncatedOutputText(truncation: TruncationResult): string {
	if (!truncation.truncated) return truncation.content;
	return `[Output truncated to last ${truncation.outputLines} of ${truncation.totalLines} lines / ${formatSize(truncation.outputBytes)} of ${formatSize(truncation.totalBytes)}]\n${truncation.content}`;
}

function truncationIndicator(details: ArtifactDetails): string {
	const truncated: string[] = [];
	if (details.stdoutTruncation.wasTruncated) truncated.push(`stdout ${details.stdoutTruncation.originalSize}`);
	if (details.stderrTruncation.wasTruncated) truncated.push(`stderr ${details.stderrTruncation.originalSize}`);
	return truncated.length > 0 ? `output: truncated ${truncated.join(", ")}` : "output: complete";
}

function displayArg(arg: string): string {
	return /^[A-Za-z0-9_./:@%+=,-]+$/.test(arg) ? arg : JSON.stringify(arg);
}

function deploySummaryLines(deploy: DeployJson | undefined, theme: Theme): string[] {
	if (!deploy) return [theme.fg("dim", "deploy JSON: not parsed")];
	const parts = [
		deploy.site ? `site=${deploy.site}` : undefined,
		deploy.release ? `release=${deploy.release}` : undefined,
		deploy.url ? `url=${deploy.url}` : undefined,
		deploy.dry_run !== undefined ? `dry_run=${deploy.dry_run}` : undefined,
		deploy.changed !== undefined ? `changed=${deploy.changed}` : undefined,
		deploy.reused !== undefined ? `reused=${deploy.reused}` : undefined,
		deploy.deleted !== undefined ? `deleted=${deploy.deleted}` : undefined,
	].filter((part): part is string => part !== undefined);
	const lines = [theme.fg("muted", `deploy JSON: ${parts.join(" · ") || "parsed"}`)];
	if (deploy.summary) {
		const summary = [
			deploy.summary.added_count !== undefined ? `added=${deploy.summary.added_count}` : undefined,
			deploy.summary.changed_count !== undefined ? `changed=${deploy.summary.changed_count}` : undefined,
			deploy.summary.deleted_count !== undefined ? `deleted=${deploy.summary.deleted_count}` : undefined,
			deploy.summary.excluded_count !== undefined ? `excluded=${deploy.summary.excluded_count}` : undefined,
		].filter((part): part is string => part !== undefined);
		if (summary.length > 0) lines.push(theme.fg("muted", `deploy summary: ${summary.join(" · ")}`));
	}
	return lines;
}

class WidthAwareText implements Component {
	private readonly lines: string[];

	constructor(text: string | string[]) {
		this.lines = (Array.isArray(text) ? text : [text]).flatMap((line) => line.split("\n"));
	}

	render(width: number): string[] {
		const maxWidth = Math.max(0, width);
		return this.lines.map((line) => truncateToWidth(line, maxWidth));
	}

	invalidate(): void {
		// Immutable render input; no cache to clear.
	}
}

export default function (pi: ExtensionAPI) {
	const artifactTool = defineTool({
		name: "artifact",
		label: "Artifact",
		description: `Create a previewable OpenQuick web artifact from UTF-8 text files, or a generated Code Mode editor artifact, under \`${ARTIFACT_ROOT}/<site>\` and run \`quick deploy <artifact-dir> --site <site> --profile ${PROFILE} --no-build --json\` for \`https://<site>.sammy.sh\`. Use for shareable static demos, mockups, HTML/CSS/JS pages or sites, and \`mode: "codemode"\` code-playground artifacts. Do not use for custom servers, backend code, secrets, environment variables, or binary asset uploads. Side effects: deletes/recreates \`${ARTIFACT_ROOT}/<site>\` under the current workspace and writes files before deploy, even with \`dryRun: true\` or if deploy later fails. \`dryRun: true\` adds \`--dry-run\` so OpenQuick produces a deploy plan instead of publishing. On success, details include site, URL, mode, SDK flag, artifact directory, written paths, command, truncated stdout/stderr, and truncation metadata; stdout/stderr in details and failures are limited to the last ${DEFAULT_MAX_LINES} lines or ${formatSize(DEFAULT_MAX_BYTES)}.`,
		promptSnippet: `Create and publish static HTML/CSS/JS artifacts with OpenQuick (quick deploy --profile ${PROFILE}). Use mode='codemode' for an SDK-backed code editor/live-preview artifact.`,
		promptGuidelines: [
			"Use artifact when the user asks for a previewable web artifact, static demo, mockup, or shareable HTML/CSS/JS page.",
			"Set mode='codemode' when the user wants code-mode/code-playground behavior: editable files, sandboxed live preview, OpenQuick SDK identity, and quick.db persistence in the editor shell.",
			"For static artifacts that need OpenQuick APIs, set sdk=true so the page imports /_quick/sdk.js and exposes window.quick.",
			"artifact deploys to the OpenQuick cf profile with quick deploy --profile cf; return the resulting URL to the user.",
			"artifact is for static files only. Do not use artifact for custom servers, secrets, environment variables, or backend code.",
		],
		parameters: ArtifactParamsSchema,
		executionMode: "sequential",

		async execute(_toolCallId, params, signal, onUpdate, ctx) {
			const mode = params.mode ?? "static";
			const files = filesFromParams(params);
			const site = deriveSite(params, files);
			const workspaceRoot = resolve(ctx.cwd);
			const artifactBase = resolve(ctx.cwd, ARTIFACT_ROOT);
			const artifactDir = resolve(artifactBase, site);

			return withFileMutationQueue(artifactDir, async () => {
				onUpdate?.({ content: [{ type: "text", text: `Writing ${files.length} artifact file(s) for ${site}...` }], details: { site, mode, artifactDir } });
				await writeArtifactFiles(workspaceRoot, artifactBase, artifactDir, files);

				const command = [
					"deploy",
					artifactDir,
					"--site",
					site,
					"--profile",
					PROFILE,
					"--no-build",
				];
				if (params.overwrite === true) command.push("--yes");
				if (params.dryRun) command.push("--dry-run");
				command.push("--json");

				onUpdate?.({ content: [{ type: "text", text: `${params.dryRun ? "Dry-running" : "Deploying"} ${site} with quick deploy --profile ${PROFILE}...` }], details: { site, mode, artifactDir, command } });
				const result = await pi.exec("quick", command, { signal });
				const stdoutTruncation = truncateTail(result.stdout ?? "");
				const stderrTruncation = truncateTail(result.stderr ?? "");
				const stdoutText = truncatedOutputText(stdoutTruncation);
				const stderrText = truncatedOutputText(stderrTruncation);
				const deploy = parseDeployJson(result.stdout ?? "");
				const url = deploy?.url ?? `https://${site}.sammy.sh`;

				const details: ArtifactDetails = {
					site,
					profile: PROFILE,
					url,
					release: deploy?.release,
					mode,
					sdk: mode === "codemode" || Boolean(params.sdk),
					dryRun: Boolean(params.dryRun),
					artifactDir,
					files: files.map((file) => file.path),
					command: ["quick", ...command],
					stdout: stdoutText,
					stderr: stderrText,
					stdoutTruncated: stdoutTruncation.truncated,
					stderrTruncated: stderrTruncation.truncated,
					stdoutTruncation: outputTruncationDetails(stdoutTruncation),
					stderrTruncation: outputTruncationDetails(stderrTruncation),
					deploy,
				};

				if (result.code !== 0) {
					const overwriteHint = params.site && params.overwrite !== true
						? "\nIf you intended to update an existing site, pass overwrite: true to opt into quick deploy --yes."
						: "";
					throw new Error(`quick deploy failed with exit code ${result.code}.\nstdout:\n${stdoutText}\nstderr:\n${stderrText}${overwriteHint}`);
				}

				const summary = params.dryRun
					? `Artifact ${mode} dry run succeeded for ${site}. Planned URL: ${url}`
					: mode === "codemode"
						? `OpenQuick Code Mode artifact deployed: ${url}`
						: `Artifact deployed: ${url}`;
				return {
					content: [{ type: "text", text: summary }],
					details,
				};
			});
		},

		renderCall(args, theme, _context) {
			const mode = args.mode ?? "static";
			const site = args.site ? `site:${args.site}` : "site:auto";
			const flags = [args.dryRun ? "dry-run" : undefined, args.overwrite ? "overwrite" : undefined].filter((flag): flag is string => flag !== undefined);
			const suffix = flags.length > 0 ? ` ${flags.join(" ")}` : "";
			return new WidthAwareText(theme.fg("toolTitle", theme.bold("artifact ")) + theme.fg("accent", mode) + theme.fg("muted", ` ${site}${suffix}`));
		},

		renderResult(result, { expanded, isPartial }, theme, _context) {
			if (isPartial) {
				return new WidthAwareText(theme.fg("warning", "artifact running..."));
			}

			const details = result.details as ArtifactDetails | undefined;
			if (!details) {
				const text = result.content[0];
				return new WidthAwareText(text?.type === "text" ? text.text : "");
			}

			const status = details.dryRun ? "Artifact dry run" : "Artifact deployed";
			const lines = [
				theme.fg("success", `✓ ${status}: ${details.url ?? `https://${details.site}.sammy.sh`}`),
				theme.fg("muted", `mode: ${details.mode} · files: ${details.files.length} · ${truncationIndicator(details)}`),
			];

			if (expanded) {
				lines.push(theme.fg("dim", `command: ${details.command.map(displayArg).join(" ")}`));
				lines.push(theme.fg("dim", `artifactDir: ${details.artifactDir}`));
				lines.push(theme.fg("muted", "files:"));
				for (const file of details.files) lines.push(theme.fg("dim", `  - ${file}`));
				lines.push(...deploySummaryLines(details.deploy, theme));
			}

			return new WidthAwareText(lines);
		},
	});

	pi.registerTool(artifactTool);
}
