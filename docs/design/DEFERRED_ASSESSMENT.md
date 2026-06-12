# Deferred assessment

<!-- markdownlint-disable MD013 -->

Status: assessment ratified; user lifted blanket v2/v3 deferral  
Date: June 2026  
Related parity baseline: [QUICK_PARITY.md](./QUICK_PARITY.md)

## Evidence note

This assessment is a product and implementation-triage note for capabilities previously treated as deferred or not adopted. It should be read after `QUICK_PARITY.md`, which records the parity evidence, repo references, and alternative reimplementations that motivated this backlog. The orchestrator task supplied the ratified verdict list and sizes; the full prose payload was not present in the task body, so rationale and sketches below are normalized from the supplied verdicts, `QUICK_PARITY.md`, and the existing OpenQuick design stance rather than from an additional source transcript.

Verdict vocabulary:

- **IN** — accepted into the OpenQuick backlog now that the blanket v2/v3 deferral has been lifted. The size marker is a rough implementation size: **S**, **M**, or **L**.
- **REJECTED** — explicitly not adopted for this product line; keep as a non-goal unless a later assessment reverses the decision.

## Summary table

| # | Capability / idea | Verdict | Size | Rationale summary |
| --- | --- | --- | --- | --- |
| 1 | AI proxy | IN | L | Core Quick capability, but needs server-side keys, budgets, rate limits, and policy controls. |
| 2 | Warehouse named-query proxy over SQLite | IN | M | Accept a constrained named-query model instead of arbitrary warehouse access. |
| 3 | Admin stats / TUI health | IN | M | Operators need visibility before enabling higher-risk APIs and public surfaces. |
| 4 | Custom domain catalog plus restricted on-demand TLS ask | IN | L | Custom domains are useful if issuance is limited to verified catalog entries. |
| 5 | Owner / namespace mode | REJECTED | — | Conflicts with the no-owners, org-wide overwrite model. |
| 6 | Postgres adapter | REJECTED | — | Adds operator and maintenance burden outside the SQLite single-host stance. |
| 7 | Object storage uploads | REJECTED | — | Adds a cloud dependency to a local-disk upload model. |
| 8 | Multi-host replication | REJECTED | — | Breaks the simple single-host architecture and adds consistency complexity. |
| 9 | Signed release manifests | IN | M | Strengthens provenance and rollback/audit confidence for immutable releases. |
| 10 | Background jobs | REJECTED | — | Turns OpenQuick into a compute/cron platform, which is out of scope. |
| 11 | SSH cert deploy audit | IN | M | Deploy access is SSH-based; certificate metadata should feed the audit trail. |
| 12 | External audit sinks | REJECTED | — | Local signed audit records are enough for the core product; sinks create integration sprawl. |
| 13 | Browser deploy portal, config-gated | IN | L | Useful and common in rebuilds, but must be explicitly enabled and hardened. |
| 14 | Alias subdomains | IN | M | Completes the existing partial `site != subdomain` story. |
| 15 | `quick delete` | IN | M | Users need a top-level cleanup workflow, not only internal daemon deletion. |
| 16 | Overwrite friction | IN | M | Preserves no-owners while reducing accidental overwrites of another deployer's site. |
| 17 | Per-site public toggle | IN | L | Useful for static demos only when paired with explicit guardrails. |
| 18 | Static-only scan | IN | M | Required guardrail before public static mode can safely bypass identity. |
| 19 | ZIP deploys | IN | M | Useful for portals and artifact bundles if extraction is capped and validated. |
| 20 | File listing, config-gated | IN | M | Helpful for artifact dumps, but must be opt-in because it exposes file trees. |
| 21 | JSX rendering | REJECTED | — | Conflicts with the static-byte-serving contract and injects supply-chain risk. |
| 22 | Local dev versus deployed API | IN | L | Helps agents iterate against real site state, but needs careful auth and data-safety design. |
| 23 | `.skill` artifact | IN | S | Low-cost packaging of existing agent guidance for portable coding-agent use. |
| 24 | Hash-diff upload | REJECTED | — | Duplicates rsync's value in the current SSH transport model. |
| 25 | Serverless substrate | REJECTED | — | Conflicts with OpenQuick's self-hosted single-host default. |
| 26 | Portal i18n | REJECTED | — | Premature while the browser portal itself is not yet shipped. |
| 27 | Cloudflare Access automation | REJECTED | — | Vendor-specific automation is outside the generic IAP stance. |
| 28 | Per-site ACL rules | REJECTED | — | Reintroduces ownership/permissions complexity rejected by the product model. |
| 29 | Cloud-internals parity | REJECTED | — | GCS/gcsfuse/CloudSQL/NGINX are Shopify implementation details, not OpenQuick requirements. |
| 30 | Synthetic corporate identity fields | REJECTED | — | OpenQuick should pass through real provider claims, not fabricate org metadata. |

Summary counts:

| Verdict | Count |
| --- | ---: |
| IN | 16 |
| REJECTED | 14 |
| Total | 30 |

Accepted size counts:

| Size | Count |
| --- | ---: |
| L | 5 |
| M | 10 |
| S | 1 |

## Per-item assessment

### 1. AI proxy

- **Verdict:** IN (L)
- **Rationale:** The AI proxy is a prominent Shopify Quick capability and remains valuable for agent-created static apps because it keeps provider keys server-side and exposes a zero-config browser API. The earlier v2/v3 deferral was warranted because AI introduces cost, abuse, policy, and secret-management risk; lifting the blanket deferral means those controls become part of the feature rather than reasons to exclude it.
- **Sketch:** Add host-managed AI provider configuration, model allowlists, per-site and per-identity rate/budget limits, audit events, and SDK methods such as `quick.ai.chat` and image generation. Keep client calls same-origin under `/_quick`, never expose provider keys, and ship with the capability disabled until configured by the operator.

### 2. Warehouse named-query proxy over SQLite

- **Verdict:** IN (M)
- **Rationale:** Full arbitrary warehouse access is too broad, but a named-query proxy gives sites useful analytics-style reads while preserving operator control. Anchoring the first version to SQLite keeps the implementation aligned with the current single-host storage model rather than introducing BigQuery or another external warehouse dependency.
- **Sketch:** Define a catalog of named read-only SQL queries with typed parameters and result limits. Expose only named calls through `/_quick/warehouse/:name` and an SDK wrapper; reject ad hoc SQL, writes, unbounded scans, and queries not explicitly registered by the host.

### 3. Admin stats / TUI health

- **Verdict:** IN (M)
- **Rationale:** Higher-risk backlog items need operator visibility into site count, release count, storage, uploads, realtime use, API use, rate limits, and failed auth/deploy activity. TUI/admin health also makes the single-host operating model easier to support without adding external observability products.
- **Sketch:** Add daemon stats endpoints and TUI/admin views for host health, disk usage, SQLite status, active realtime connections, deploy activity, public-site state, and high-cost API counters. Keep the surface operator-only and reuse existing catalog/audit data where possible.

### 4. Custom domain catalog plus restricted on-demand TLS ask

- **Verdict:** IN (L)
- **Rationale:** Custom domains are a practical requirement for real adoption, but unrestricted on-demand certificate issuance is an abuse and operational risk. The accepted shape is catalog-driven: only verified domains mapped to a site are eligible for routing and TLS issuance.
- **Sketch:** Add a domain catalog with hostname, site mapping, verification status, and audit metadata. Provide an `ask` endpoint for an ACME/TLS terminator that returns allow only for verified catalog entries, and deny unknown domains by default. Keep wildcard/base-domain routing unchanged for normal OpenQuick sites.

### 5. Owner / namespace mode

- **Verdict:** REJECTED
- **Rationale:** Owner and namespace semantics conflict with the Quick-style model where authenticated deployers can overwrite sites and the organization boundary, not per-site ownership, is the primary access control. This would also pull in ACL, transfer, delegation, and support flows that are intentionally absent.
- **Sketch:** Do not implement owner or namespace mode. Use global deploy access, audit trails, signed release manifests, and overwrite friction to make the no-owner model safer without changing its semantics.

### 6. Postgres adapter

- **Verdict:** REJECTED
- **Rationale:** PostgreSQL adds deployment, backup, migration, pooling, and support requirements that work against the current one-host SQLite design. None of the accepted items require a second database substrate.
- **Sketch:** Keep SQLite as the core store. Revisit only if a future scale assessment proves SQLite is the blocker and identifies a bounded migration plan.

### 7. Object storage uploads

- **Verdict:** REJECTED
- **Rationale:** Object storage for uploads would add provider credentials, bucket policy, lifecycle, and consistency concerns to a local-disk upload model. It also widens the storage substrate without being necessary for the accepted backlog.
- **Sketch:** Keep uploads on local disk in the core product. Future backup/export tooling may copy data elsewhere, but the user-visible upload API should not require object storage.

### 8. Multi-host replication

- **Verdict:** REJECTED
- **Rationale:** Multi-host replication conflicts with the single-VM/single-host architecture and introduces distributed consistency, failover, routing, and conflict-resolution complexity. It is a different product tier, not a deferred parity feature.
- **Sketch:** Keep OpenQuick single-host. Operators needing high availability should solve it outside the core product with host-level backups, snapshots, or a separate future assessment.

### 9. Signed release manifests

- **Verdict:** IN (M)
- **Rationale:** OpenQuick already treats releases as immutable units; signed manifests add tamper evidence and a durable link between files, deployer identity, and activation. This supports rollback, audit, and future public/custom-domain features.
- **Sketch:** Generate a manifest per release containing file paths, sizes, hashes, deploy metadata, and parent/previous release references. Sign it with a host-managed key or deploy-signing mechanism, verify on activation where practical, and expose signature status in admin/TUI views.

### 10. Background jobs

- **Verdict:** REJECTED
- **Rationale:** Background jobs would turn OpenQuick into an application runtime with cron/queue semantics, retry policy, secrets, and long-running compute. That conflicts with the static-sites-plus-shared-APIs boundary.
- **Sketch:** Do not add a job runner or cron API. Sites that need background compute should use external systems and call OpenQuick APIs explicitly where allowed.

### 11. SSH cert deploy audit

- **Verdict:** IN (M)
- **Rationale:** Deploys already depend on SSH access, so SSH certificate identity is valuable audit evidence. Capturing certificate principals, key IDs, serials, and connection metadata improves accountability without adding site owners.
- **Sketch:** Plumb deployer identity from SSH/sshd environment or the deploy handshake into prepare/activate records. Store certificate principal/key metadata in the audit log and release manifest, and surface it in `quick list`, admin stats, and overwrite warnings.

### 12. External audit sinks

- **Verdict:** REJECTED
- **Rationale:** External SIEM/webhook/log sinks create provider-specific configuration, delivery guarantees, secret handling, and support scope. They are not required for the core audit improvements accepted here.
- **Sketch:** Keep audit records local and exportable. Do not add built-in external sink integrations in this assessment.

### 13. Browser deploy portal, config-gated

- **Verdict:** IN (L)
- **Rationale:** Browser deploys appear repeatedly in Quick rebuilds and are a useful complement to CLI/TUI workflows. They are also a new upload and activation surface, so they must be disabled unless explicitly enabled and must inherit identity, CSRF, size, and rate-limit protections.
- **Sketch:** Add an authenticated portal under the apex/directory surface that supports drag-and-drop folder or ZIP deploys. Route through the same staging, manifest, overwrite-friction, static-scan, and activation pipeline as CLI deploys. Ship behind a host config flag with clear operator warnings.

### 14. Alias subdomains

- **Verdict:** IN (M)
- **Rationale:** The current product partially exposes subdomain selection but still largely ties routing and catalog identity to the site name. True alias support lets a site have a stable internal name and one or more public host labels without introducing owners.
- **Sketch:** Add an alias table with unique host labels mapped to site IDs. Update host routing, URL generation, deploy validation, list output, and conflict checks so `site != subdomain` works consistently. Preserve existing site-name host behavior as the default alias.

### 15. `quick delete`

- **Verdict:** IN (M)
- **Rationale:** Users need a top-level delete workflow for mistakes, experiments, and cleanup. Internal daemon deletion is not enough if the normal user surface has no safe, audited command.
- **Sketch:** Add `quick delete <site>` with dry-run output, typed confirmation or `--yes`, audit logging, and clear scope for deleting releases, current alias mappings, uploads, database collections, and public/custom-domain state. Prefer tombstones or retention windows if needed for recovery.

### 16. Overwrite friction

- **Verdict:** IN (M)
- **Rationale:** OpenQuick should keep the no-owner model, but accidental overwrite of a site last deployed by someone else is a foreseeable failure mode. A typed confirmation preserves openness while making destructive intent explicit.
- **Sketch:** During deploy prepare, compare the current release deployer to the new deployer. If they differ, print who last deployed and require typing the site name unless `--yes` or another explicit non-interactive override is supplied. Record the bypass in the audit log.

### 17. Per-site public toggle

- **Verdict:** IN (L)
- **Rationale:** Some static sites are meant to be shared outside the trusted organization, but the default OpenQuick stance remains identity-gated. Public mode is acceptable only as an explicit per-site exception with static-only constraints and strong audit visibility.
- **Sketch:** Add a per-site `public_static` state gated by global host policy. Public requests may serve static files only; `/_quick/*` and other dynamic APIs stay identity-protected. Require the static-only scan before enabling or deploying to a public site, and show public state prominently in list/admin views.

### 18. Static-only scan

- **Verdict:** IN (M)
- **Rationale:** Public static mode needs a guardrail that prevents accidentally exposing apps that depend on identity-bound APIs. A conservative scan can catch common SDK and `/_quick` usage before identity is bypassed for static assets.
- **Sketch:** Scan deployable text files for OpenQuick SDK/API usage such as `/_quick`, `quick.db`, `quick.uploads`, `quick.ai`, `quick.identity`, realtime endpoints, and direct API calls. Fail public-mode enablement or public-site deploys on matches unless a future assessment defines an explicit override policy.

### 19. ZIP deploys

- **Verdict:** IN (M)
- **Rationale:** ZIP input is useful for browser portals, exported artifacts, and simple sharing. It is safe only if extraction is treated as untrusted input with strict caps and path validation.
- **Sketch:** Add ZIP deploy support that rejects encrypted archives, symlinks, device files, absolute paths, path traversal, duplicate entries, and excessive entry counts or uncompressed size. Normalize common single-root archives and feed extracted files into the normal staging/manifest/activation flow.

### 20. File listing, config-gated

- **Verdict:** IN (M)
- **Rationale:** File listings help with artifact dumps and ad hoc sharing, but they expose directory structure and may surprise users who expected an `index.html`-only site. The feature should therefore be opt-in.
- **Sketch:** Add a global and/or per-site setting that renders an escaped directory listing when no index file exists. Keep listings disabled by default, hide internal OpenQuick paths, and audit/list which sites have listing enabled.

### 21. JSX rendering

- **Verdict:** REJECTED
- **Rationale:** Automatic JSX rendering would mean OpenQuick transforms uploaded bytes and injects runtime dependencies such as React, Babel, or third-party CDNs. That conflicts with the contract that OpenQuick serves the uploaded static output.
- **Sketch:** Do not auto-render JSX. Users can ship prebuilt HTML/JS or include their own client-side tooling explicitly in their site files.

### 22. Local dev versus deployed API

- **Verdict:** IN (L)
- **Rationale:** Agent and developer workflows benefit from running a local frontend against real deployed site APIs, data, uploads, and realtime channels. The risk is accidental mutation or leakage of production-like data, so the feature needs deliberate authentication and warnings.
- **Sketch:** Add a local dev proxy mode that authenticates to a selected deployed site and forwards `/_quick/*` while serving local static files. Make read/write behavior explicit, show the target site prominently, avoid browser-exposed provider secrets, and log remote API use as the authenticated developer.

### 23. `.skill` artifact

- **Verdict:** IN (S)
- **Rationale:** OpenQuick already generates agent-oriented guidance; packaging it as a portable `.skill` artifact makes the same knowledge easier to install in coding-agent environments with minimal product risk.
- **Sketch:** Produce a versioned skill archive containing `SKILL.md`, SDK/API references, deploy constraints, and examples. Add it to release artifacts or expose it through `quick init` without changing the runtime platform.

### 24. Hash-diff upload

- **Verdict:** REJECTED
- **Rationale:** The current SSH+rsync deploy transport already provides efficient delta transfer and avoids designing a parallel HTTP hash negotiation protocol. Hash-diff upload adds complexity without enough benefit in the accepted architecture.
- **Sketch:** Keep rsync as the efficient diff mechanism for CLI deploys. Browser deploys should use simpler bounded uploads unless a later performance assessment justifies chunk/hash negotiation.

### 25. Serverless substrate

- **Verdict:** REJECTED
- **Rationale:** A Cloudflare Workers/R2/Durable Objects or other serverless substrate is a materially different product architecture. OpenQuick's core promise is a self-hosted, SSH-reachable single host running `quickd`.
- **Sketch:** Do not port the core runtime to serverless as part of this backlog. Provider-specific experiments belong in separate integrations or forks, not in the main substrate.

### 26. Portal i18n

- **Verdict:** REJECTED
- **Rationale:** Internationalizing a portal that is not yet shipped is premature and would add strings, routing, language preference, and review overhead before the product surface stabilizes.
- **Sketch:** Keep future portal copy simple and default-language only until the portal exists and usage demonstrates the need for localization.

### 27. Cloudflare Access automation

- **Verdict:** REJECTED
- **Rationale:** OpenQuick supports multiple identity-aware proxy patterns; automating Cloudflare Access apps would make the core product vendor-specific and require Cloudflare API credentials, permission models, and drift handling.
- **Sketch:** Continue documenting Cloudflare Access setup for operators. Do not add built-in Access app creation, bypass app automation, or provider-specific public toggle machinery in this assessment.

### 28. Per-site ACL rules

- **Verdict:** REJECTED
- **Rationale:** Per-site ACLs reintroduce ownership and permissions complexity and undermine the discoverable organization-wide Quick model. They also create confusing interactions with overwrite, public mode, and custom domains.
- **Sketch:** Keep access control at the IAP/trust-boundary level plus explicit public-static exceptions. Do not add per-site viewer/editor ACLs.

### 29. Cloud-internals parity

- **Verdict:** REJECTED
- **Rationale:** GCS, gcsfuse, CloudSQL, and NGINX wildcard mapping are Shopify implementation details, not product requirements. OpenQuick intentionally uses local releases, SQLite, and Go routing to stay self-hosted and simple.
- **Sketch:** Preserve product-level compatibility where it matters, but do not chase cloud-internal parity for its own sake.

### 30. Synthetic corporate identity fields

- **Verdict:** REJECTED
- **Rationale:** Identity fields such as title, team, or Slack handle should reflect real provider claims. Fabricating them would mislead applications and users and would not create true Shopify corporate-context parity.
- **Sketch:** Continue normalizing and passing through available provider claims/capabilities. Document that missing corporate fields require IAP/provider configuration rather than OpenQuick synthesis.

## Implementation waves

### Wave 1 — Provenance, audit, and low-risk packaging

Accepted items: #23 `.skill` artifact, #9 signed release manifests, #11 SSH cert deploy audit, #16 overwrite friction, #15 `quick delete`, #14 alias subdomains.

Goals:

- Establish deploy provenance before adding browser or public deploy surfaces.
- Make destructive operations explicit and auditable without adding owners or ACLs.
- Complete the current alias-subdomain partial so later custom-domain work has a clean catalog model.

Exit checks:

- Release manifests include hashes and deploy identity.
- Deploy audit records include available SSH certificate metadata.
- Overwrite/delete flows support non-interactive safe overrides for automation.
- Alias routing and URL generation agree for existing site-name hosts and new aliases.

### Wave 2 — Guarded browser and public static surfaces

Accepted items: #3 admin stats/TUI health, #18 static-only scan, #20 file listing config-gated, #19 ZIP deploys, #13 browser deploy portal, #17 per-site public toggle.

Goals:

- Add operator visibility before exposing new upload/public paths.
- Land reusable safety checks for ZIP extraction, file listing, and public static mode.
- Keep the browser portal and public toggle disabled unless the host explicitly opts in.

Exit checks:

- Public mode cannot serve `/_quick/*` anonymously.
- Public-site deploys run the static-only scan.
- ZIP extraction enforces traversal, symlink, entry-count, and size limits.
- TUI/admin views show portal/public/listing state and relevant counters.

### Wave 3 — High-trust APIs and external-facing routing

Accepted items: #2 warehouse named-query proxy over SQLite, #1 AI proxy, #4 custom domain catalog plus restricted on-demand TLS ask, #22 local dev versus deployed API.

Goals:

- Add high-value APIs only with explicit host configuration, limits, and audit.
- Route custom domains only through verified catalog entries.
- Support local agent/dev workflows without exposing provider secrets or hiding production-data risk.

Exit checks:

- AI and warehouse capabilities report disabled until configured.
- Named warehouse queries are read-only, bounded, and not ad hoc SQL.
- TLS ask denies unknown or unverified hostnames by default.
- Local dev remote-API mode shows the target site and logs remote API use.

### Non-goal guardrail across all waves

Rejected items #5, #6, #7, #8, #10, #12, #21, #24, #25, #26, #27, #28, #29, and #30 remain out of scope while implementing the accepted backlog. Do not smuggle them in as dependencies for an accepted item; require a new assessment if an accepted implementation appears to need one of those rejected capabilities.
