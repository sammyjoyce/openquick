# UnJS ecosystem and unpkg assessment for the OpenQuick SDK

Status: assessed 2026-06-12; documented only; no UnJS runtime or build dependency adopted.

## Constraints that drive the decision

- The browser SDK is a same-origin module served by quickd at `/_quick/sdk.js`.
- The SDK is embedded into quickd from `server/internal/api/sdk/quick.js`, so the
  served SDK version matches the host API implementation.
- Hosted sites get a zero-config promise: no API keys, no external runtime URLs,
  and no package install step just to call `/_quick/*`.
- The SDK is currently a single ESM file with zero runtime dependencies.
- OpenQuick should work self-hosted and air-gapped. CDN/package-registry access is
  acceptable for optional developer workflows, not for the default hosted-site
  runtime.
- SDK requests intentionally use leading-slash paths such as `/_quick/identity`,
  `/_quick/db/:collection`, and `/_quick/realtime` so they resolve against the
  page's OpenQuick origin.
- Static JavaScript can be shared across sibling OpenQuick sites through the
  sibling-site CORS mechanism, but the `/_quick/*` API remains same-origin.

## Measurement method

Current SDK artifact:

| Artifact | Raw bytes | gzip `-n` bytes |
| --- | ---: | ---: |
| `sdk/js/dist/quick.js` built with `bun build src/index.ts --outfile dist/quick.js --format esm` | 14,968 | 3,925 |

Runtime-package prototype was built in `/tmp/openquick-unjs-sdk-assessment` with
`ofetch@1.5.1`, `ufo@1.6.4`, and `destr@2.0.5` installed. Each variant copied
`sdk/js/src/index.ts` to the same scratch path, changed only the relevant hot
path, and used the same Bun build command as the repository.

| Prototype | What changed | Raw bytes | gzip `-n` bytes | gzip delta |
| --- | --- | ---: | ---: | ---: |
| Baseline copy | No code change | 14,968 | 3,925 | 0 |
| `ufo` only | `apiPath()` uses `joinURL('/_quick', ...)` | 16,315 | 4,389 | +464 |
| `destr` only | WebSocket and SSE `JSON.parse` sites use `destr()` | 16,983 | 4,545 | +620 |
| `ofetch` only | `requestJson()` uses `$fetch()` | 34,700 | 8,951 | +5,026 |
| `ofetch` + `ufo` + `destr` | All three changes above | 34,690 | 8,946 | +5,021 |

Build-system prototype was built in `/tmp/openquick-unbuild-assessment` with
`unbuild@3.6.1` and `typescript@5.9.2`.

| Build path | JS artifact | JS raw bytes | JS gzip `-n` bytes | Types output | Observed build time | Install footprint |
| --- | --- | ---: | ---: | --- | ---: | --- |
| Current Bun build | `dist/quick.js` | 14,968 | 3,925 | none | 0.063s wall time | no `node_modules` required |
| `unbuild` prototype | `dist-unbuild/quick.mjs` | 15,134 | 3,965 | `quick.d.ts` and `quick.d.mts`, 4,098 bytes each | 1.023s wall time | 68 MiB `node_modules`, 120 top-level dirs |

The `unbuild` output was still a single zero-runtime-dependency ESM artifact and
its exports matched the SDK shape, but adopting it would require a build config,
installing dev dependencies before CI builds, and adapting the embed path or
output filename.

## Package assessment

| Package | Plausible role in OpenQuick | Measured size data | Verdict | Rationale |
| --- | --- | --- | --- | --- |
| `ofetch@1.5.1` | Replace `requestJson()`, error parsing, optional retry/backoff, and future request/response interceptors. | Replacing `requestJson()` with `$fetch()` raised the SDK from 3,925 gzip bytes to 8,951 bytes (+5,026). The combined `ofetch`/`ufo`/`destr` prototype was 8,946 gzip bytes (+5,021). | Not now | The conveniences are real, especially `$fetch.create()`, interceptors, typed responses, and retry hooks. They are not worth more than doubling the gzip size for a same-origin SDK whose current helper is small and whose state-changing DB/upload calls should not gain generic retries accidentally. It would also change error shapes and add runtime supply-chain surface. Revisit only if SDK-wide retry/backoff, telemetry, or auth hooks become a repeated need. |
| `ufo@1.6.4` | Replace `apiPath()` and future query-string helpers. | `ufo`-only prototype added +464 gzip bytes. | Not now | All current endpoints are leading-slash `/_quick/*` paths with path segments encoded by a tiny local helper. There is no complex URL normalization or query construction today. A dependency for this would be larger than the code it replaces. |
| `destr@2.0.5` | Replace defensive `JSON.parse` calls for realtime/SSE payloads and possibly response parsing. | `destr`-only prototype added +620 gzip bytes. | Not now | `destr` is useful when parsing unknown or hostile JSON-like input without throwing. The SDK currently parses same-origin JSON from quickd, plus WebSocket/SSE frames already guarded by `try`/`catch`. Its safety properties do not materially improve the present trust boundary and would change parser semantics. Revisit if the SDK starts ingesting third-party JSON. |
| `uncrypto@0.1.3` | Provide a Web Crypto facade across runtimes. | No SDK bundle prototype because the current SDK has no crypto call sites; npm unpacked size is 7,871 bytes. | Not now | Browser-hosted OpenQuick code can use the platform `crypto` object directly if a future feature needs random IDs or signatures. The server-side crypto surface is Go. There is no current SDK code to replace. |
| `unbuild@3.6.1` | Replace the Bun build with Rollup/mkdist and emit TypeScript declarations for an npm-publishable package. | Prototype JS was +40 gzip bytes and emitted clean `.d.ts`/`.d.mts`, but needed 68 MiB of dev dependencies and a config file and took about 1.0s vs 0.063s for Bun. | Not now | Proper declaration emission is the strongest UnJS win. It is still not a clear low-risk win while the package is private and CI intentionally builds the SDK without an install step. Revisit when publishing `@openquick/sdk` to npm or when `.d.ts` artifacts become required. |
| `changelogen@0.6.2` | Generate release changelogs from Conventional Commits. | Not bundled into the SDK; npm unpacked size is 43,089 bytes before dependencies. | Not now | This is release automation, not SDK runtime or SDK build simplification. It may be useful later if OpenQuick's release process becomes fully Conventional-Commit-driven. |
| `automd@0.4.3` | Generate or refresh README/API sections. | Not bundled into the SDK; npm unpacked size is 64,045 bytes before dependencies. | Not now | The SDK API is small and currently documented by hand. README automation would add another Node toolchain without reducing risk. |
| `undocs@0.4.16` | Provide a UnJS-style docs site/theme. | Not bundled into the SDK; npm unpacked size is 360,871 bytes before its Nuxt/Vue dependency tree. | Never for the SDK; not now for project docs | OpenQuick's SDK should not depend on a docs framework. A separate docs website could be considered later, but it should not affect the SDK artifact or quickd runtime. |

## Candidate adoption decisions

### Candidate A: switch SDK build to `unbuild`

Not adopted.

The scratch build proved that `unbuild` can produce a single-file ESM artifact
with similar size and clean declaration files. The adoption cost is still higher
than the current problem:

- it needs `build.config.ts`;
- it defaults to `.mjs`, so the existing `dist/quick.js` embed path would need
  more configuration or copy glue;
- it requires installing dev dependencies before `bun run build`, while CI and
  `just build-sdk` currently need only Bun;
- the scratch install added a 68 MiB `node_modules` tree;
- the observed build was about 1.0s rather than 0.063s;
- the package is still marked private and no npm publication workflow exists.

The right revisit point is npm publication or a hard requirement for committed
`.d.ts` output.

### Candidate B: npm-package polish without publishing

Not adopted.

Adding `exports` and `types` would be premature while `package.json` is private
and the build emits no declaration file. A `types` field pointing at a file that
is not produced would be misleading; pointing consumers at raw `src/index.ts`
would not be a polished npm package. A package README is useful when publication
starts, but by itself it does not change runtime or build behavior.

## unpkg and npm as a secondary distribution channel

Publishing `@openquick/sdk` to npm could make CDN imports possible:

```html
<script type="module">
  import { quick } from 'https://unpkg.com/@openquick/sdk@0.1.0/dist/quick.js';

  console.log(await quick.identity.current());
</script>
```

This works mechanically because CDN-hosted module code executes in the importing
page's `window` context. The OpenQuick SDK uses leading-slash request URLs such
as `fetch('/_quick/identity')` and `new WebSocket('/_quick/realtime')`; those are
resolved against the hosted site's document origin, not against `unpkg.com` or
`import.meta.url`. Static imports inside a CDN module would resolve relative to
the CDN module URL, so keeping the SDK single-file and dependency-free matters.

If this channel is added later, guidance should be conservative:

- Treat CDN loading as a secondary convenience for demos or agents, never the
  default production path.
- Pin exact versions such as `@openquick/sdk@0.1.0`; do not use `@latest` or
  floating semver in site code.
- Publish immutable ESM files and keep `/_quick/sdk.js` as the authoritative
  host-matched SDK.
- Generate Subresource Integrity for the exact CDN URL when possible. Inline
  `import` statements do not have an integrity attribute, so use an integrity-
  checked `<link rel="modulepreload">` or another reviewed loading pattern if
  SRI is required.
- Remember that esm.sh and similar CDNs may transform packages; a direct unpkg or
  jsDelivr file URL to OpenQuick's own ESM artifact is easier to audit.

Conflicts with OpenQuick's defaults are significant:

- Air-gapped and self-hosted deployments cannot depend on npm or a public CDN.
- CDN outages become application outages if a site relies on the CDN SDK.
- npm account compromise, registry drift, CDN compromise, and the documented
  history of npm tar extraction vulnerabilities add supply-chain risk to every
  page load.
- A CDN SDK can drift from the quickd host API. `quick.capabilities()` helps a
  site detect missing feature families, but it cannot guarantee bug-for-bug
  compatibility, changed semantics, or security-fix parity.

Recommendation: keep `/_quick/sdk.js` as the documented primary import. Consider
publishing `@openquick/sdk` only after the SDK has package metadata, declaration
files, a release process, and compatibility guidance. Even then, document CDN
imports as optional and version-pinned.

## Sibling-CORS library sites as the self-hosted analogue

OpenQuick already has the internal version of the unpkg pattern: deploy a normal
site as a shared library and import its static modules from sibling sites on the
same OpenQuick host.

```html
<script type="module">
  import { toast } from 'https://libs.quick.example.com/toast/v1/mod.js';

  toast('Loaded from a sibling OpenQuick library site');
</script>
```

This pattern fits OpenQuick better than a public CDN:

- the operator controls the library origin, deploy history, rollback, and access
  policy;
- it works on private networks and in air-gapped installations;
- it uses the existing static sibling-site CORS path for GET/HEAD assets;
- the `/_quick/*` API remains same-origin and is not opened to arbitrary sites;
- library modules that call the OpenQuick SDK's leading-slash URLs still target
  the consuming page's origin, so the same shared module can work across sites;
- versioning can be done with paths such as `/v1/mod.js` or content-hashed files.

For cookie-based IAP edges, the importing site may need credentialed module
fetches so the browser sends the IAP cookie to the library origin. That is a
browser/module-loading concern, not a reason to use a third-party CDN.

Recommendation: emphasize the sibling-library-site pattern in OpenQuick docs as
the default way to share JavaScript across sites. It serves most of the same need
as unpkg while preserving self-hosting and host-level trust boundaries.

## Server-side UnJS packages

`h3`, `nitro`, and `listhen` are not applicable to OpenQuick's server runtime.
The architecture deliberately keeps quickd in Go, using Go's HTTP, identity,
WebSocket, SQLite, filesystem, and deployment primitives. Replacing quickd or
placing a JavaScript server framework in the request path would widen the
runtime, packaging, and security surface without helping the browser SDK.

## What we adopted now

- Added this assessment document.
- Added the document to the docs index.
- Did not add SDK runtime dependencies.
- Did not switch the SDK build to `unbuild`.
- Did not add npm publication metadata yet.

## Revisit triggers

- The SDK grows repeated idempotent retry/backoff, request interception,
  observability, or auth-hook needs: remeasure `ofetch` and decide with an
  explicit retry policy for state-changing calls.
- The SDK accumulates complex query-string or URL-normalization code: reconsider
  `ufo`.
- The SDK starts parsing third-party or user-supplied JSON outside quickd's
  same-origin API: reconsider `destr`.
- Browser-side signing, verification, or random-token generation becomes an SDK
  feature: assess direct Web Crypto first, then `uncrypto` if runtime portability
  is genuinely needed.
- `@openquick/sdk` publication becomes a roadmap item: add package `exports`,
  declaration output, package README, release automation, and CDN guidance in
  one focused change.
- Manual release notes become a bottleneck after Conventional Commits are
  enforced across releases: evaluate `changelogen`.
- The documentation set grows into a hosted docs website: evaluate docs tooling
  separately from SDK and quickd runtime decisions.
