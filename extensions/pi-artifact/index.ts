import { createHash } from "node:crypto";
import { mkdir, rm, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve, sep } from "node:path";
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";

const PROFILE = "cf";
const ARTIFACT_ROOT = join(".pi", "openquick-artifacts");
const MAX_OUTPUT_LINES = 2_000;
const MAX_OUTPUT_BYTES = 50 * 1024;
const MAX_OUTPUT_LABEL = "50 KiB";

const ArtifactParamsSchema = {
	type: "object",
	additionalProperties: false,
	properties: {
		title: { type: "string", description: "Human-readable artifact title. Used to derive a site slug when site is omitted." },
		site: { type: "string", description: "Optional OpenQuick site slug. If omitted, a deterministic artifact-<title>-<hash> slug is generated." },
		html: { type: "string", description: "Shortcut for writing index.html. Use files for multi-file artifacts." },
		files: {
			type: "array",
			description: "Static files to publish. Include index.html unless html is provided. In codemode, these become the editable starter files inside the code editor.",
			items: {
				type: "object",
				additionalProperties: false,
				required: ["path", "content"],
				properties: {
					path: { type: "string", description: "Relative output path, for example index.html or assets/app.js. Absolute paths and .. are rejected." },
					content: { type: "string", description: "UTF-8 file contents." },
				},
			},
		},
		mode: {
			type: "string",
			enum: ["static", "codemode"],
			description: "static publishes files directly. codemode publishes an OpenQuick SDK-powered in-browser code editor with live preview and quick.db persistence.",
		},
		sdk: {
			type: "boolean",
			description: "For static mode, inject a /_quick/sdk.js bridge that exposes window.quick and dispatches openquick:sdk-ready. Codemode always uses the SDK.",
		},
		dryRun: { type: "boolean", description: "If true, run quick deploy --dry-run and do not publish." },
	},
} as const;

type ArtifactFileParam = {
	path: string;
	content: string;
};

type ArtifactParams = {
	title?: string;
	site?: string;
	html?: string;
	files?: ArtifactFileParam[];
	mode?: "static" | "codemode";
	sdk?: boolean;
	dryRun?: boolean;
};

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
	const cleaned = path.replace(/\\/g, "/").replace(/^\.\//, "");
	if (!cleaned || cleaned.startsWith("/") || cleaned.includes("\0")) {
		throw new Error(`Invalid artifact file path ${JSON.stringify(path)}.`);
	}
	const parts = cleaned.split("/");
	if (parts.some((part) => part === "" || part === "." || part === "..")) {
		throw new Error(`Artifact file path must be relative and must not contain empty, '.', or '..' segments: ${JSON.stringify(path)}.`);
	}
	const first = parts[0] ?? "";
	if ([".git", ".quick", ".ssh", "node_modules"].includes(first) || first.startsWith(".env")) {
		throw new Error(`Refusing to publish reserved or secret-looking path ${JSON.stringify(path)}.`);
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
    <p>Edit the files on the left. The preview has the OpenQuick SDK available as <code>window.quick</code>.</p>
    <pre id="identity">Loading identity…</pre>
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
  identityEl.textContent = me ? JSON.stringify(me, null, 2) : 'OpenQuick SDK bridge not found.';
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
    <div><strong>${escapeHtml(title)}</strong><div class="meta">OpenQuick Code Mode · SDK-backed identity + DB persistence</div></div>
    <div><span id="status">Loading…</span> <button id="save">Save to quick.db</button> <button class="primary" id="run">Run preview</button></div>
  </header>
  <div class="shell">
    <aside id="files"></aside>
    <section class="editor"><div class="bar"><span id="current"></span><span>window.quick is injected into preview</span></div><textarea id="code" spellcheck="false"></textarea></section>
    <section class="preview"><div class="bar"><span>Preview</span><span id="who"></span></div><iframe id="preview" sandbox="allow-scripts allow-same-origin allow-forms"></iframe></section>
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
    function sdkInjection() {
      return '<script type="module">import { quick } from "/_quick/sdk.js"; window.quick = quick; window.dispatchEvent(new CustomEvent("openquick:sdk-ready", { detail: { quick } }));<' + '/script>';
    }
    function renderPreview() {
      saveEditor();
      let html = files.get('index.html') || '<!doctype html><h1>No index.html</h1>';
      if (!html.includes('/_quick/sdk.js')) {
        if (/<\\/head\\s*>/i.test(html)) html = html.replace(/<\\/head\\s*>/i, sdkInjection() + '\\n</head>');
        else html = sdkInjection() + '\\n' + html;
      }
      const css = files.get('style.css');
      if (css && !html.includes('data-codemode-style')) {
        const style = '<style data-codemode-style>' + css + '</style>';
        html = /<\\/head\\s*>/i.test(html) ? html.replace(/<\\/head\\s*>/i, style + '\\n</head>') : style + html;
      }
      const js = files.get('script.js');
      if (js && !html.includes('data-codemode-script')) {
        const prelude = 'import { quick } from "/_quick/sdk.js"; window.quick = quick; window.dispatchEvent(new CustomEvent("openquick:sdk-ready", { detail: { quick } }));\\n';
        const script = '<script type="module" data-codemode-script>' + prelude + String(js).replace(/<\\/script/gi, '<\\\\/script') + '<' + '/script>';
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
        for (const [path, content] of files) {
          const existing = await docs.list({ filter: { path }, limit: 1 });
          const patch = { path, content, updatedAt: new Date().toISOString() };
          if (existing[0] && existing[0].id) await docs.update(existing[0].id, patch);
          else await docs.create(patch);
        }
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
	const hash = createHash("sha256")
		.update(JSON.stringify(files.map((file) => [file.path, file.content])))
		.digest("hex")
		.slice(0, 10);
	return validateSiteSlug(`artifact-${title}-${hash}`.slice(0, 63).replace(/-+$/g, ""));
}

async function writeArtifactFiles(root: string, files: ArtifactFileParam[]) {
	await rm(root, { recursive: true, force: true });
	await mkdir(root, { recursive: true });
	for (const file of files) {
		const target = resolve(root, file.path);
		const rel = relative(root, target);
		if (rel.startsWith("..") || rel === "" || rel.split(sep).includes("..")) {
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

function truncateOutput(text: string) {
	let bytes = 0;
	const lines = text.split(/\r?\n/);
	const kept: string[] = [];
	let truncated = false;
	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i] ?? "";
		const lineBytes = Buffer.byteLength(line, "utf8") + 1;
		if (kept.length >= MAX_OUTPUT_LINES || bytes + lineBytes > MAX_OUTPUT_BYTES) {
			truncated = true;
			break;
		}
		kept.push(line);
		bytes += lineBytes;
	}
	kept.reverse();
	const content = kept.join("\n");
	return {
		text: truncated ? `[Output truncated to last ${MAX_OUTPUT_LINES} lines / ${MAX_OUTPUT_LABEL}]\n${content}` : content,
		truncated,
	};
}

export default function (pi: ExtensionAPI) {
	pi.registerTool({
		name: "artifact",
		label: "Artifact",
		description: `Create or update a static web artifact and publish it with OpenQuick using quick deploy --profile ${PROFILE}. Supports static mode plus SDK-backed codemode (an in-browser editor/live preview using /_quick/sdk.js and quick.db). Writes files to ${ARTIFACT_ROOT}, deploys them to https://<site>.sammy.sh, and returns the URL. Output is truncated to ${MAX_OUTPUT_LINES} lines or ${MAX_OUTPUT_LABEL}.`,
		promptSnippet: `Create and publish static HTML/CSS/JS artifacts with OpenQuick (quick deploy --profile ${PROFILE}). Use mode='codemode' for an SDK-backed code editor/live-preview artifact.`,
		promptGuidelines: [
			"Use artifact when the user asks for a previewable web artifact, static demo, mockup, or shareable HTML/CSS/JS page.",
			"Set mode='codemode' when the user wants code-mode/code-playground behavior: editable files, live preview, OpenQuick SDK identity, and quick.db persistence.",
			"For static artifacts that need OpenQuick APIs, set sdk=true so the page imports /_quick/sdk.js and exposes window.quick.",
			"artifact deploys to the OpenQuick cf profile with quick deploy --profile cf; return the resulting URL to the user.",
			"artifact is for static files only. Do not use artifact for custom servers, secrets, environment variables, or backend code.",
		],
		parameters: ArtifactParamsSchema,

		async execute(_toolCallId, params, signal, onUpdate, ctx) {
			const mode = params.mode ?? "static";
			const files = filesFromParams(params);
			const site = deriveSite(params, files);
			const artifactDir = resolve(ctx.cwd, ARTIFACT_ROOT, site);

			onUpdate?.({ content: [{ type: "text", text: `Writing ${files.length} artifact file(s) for ${site}...` }], details: { site, mode, artifactDir } });
			await writeArtifactFiles(artifactDir, files);

			const command = [
				"deploy",
				artifactDir,
				"--site",
				site,
				"--profile",
				PROFILE,
				"--no-build",
				"--yes",
			];
			if (params.dryRun) command.push("--dry-run");
			command.push("--json");

			onUpdate?.({ content: [{ type: "text", text: `${params.dryRun ? "Dry-running" : "Deploying"} ${site} with quick deploy --profile ${PROFILE}...` }], details: { site, mode, artifactDir, command } });
			const result = await pi.exec("quick", command, { signal });
			const stdout = truncateOutput(result.stdout ?? "");
			const stderr = truncateOutput(result.stderr ?? "");
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
				stdout: stdout.text,
				stderr: stderr.text,
				stdoutTruncated: stdout.truncated,
				stderrTruncated: stderr.truncated,
				deploy,
			};

			if (result.code !== 0) {
				throw new Error(`quick deploy failed with exit code ${result.code}.\nstdout:\n${stdout.text}\nstderr:\n${stderr.text}`);
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
		},
	});
}
