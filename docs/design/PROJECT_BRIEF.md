# OpenQuick — Project Brief

## Goal

An open-source re-implementation of Shopify's internal "Quick" hosting platform
([Shopify Quick engineering post](https://shopify.engineering/quick)), built on
top of the curspan C23 CLI/TUI template this repo was generated from.

## Key differences from Shopify's Quick

1. **No gcloud dependency.** Shopify's `quick deploy` wraps `gcloud storage rsync`
   into a GCS bucket served via gcsfuse + NGINX. We instead use plain **rsync over
   SSH** so users can deploy to any host: exe.dev, Hetzner, a local machine, a
   homelab box, etc.
2. **Pluggable Identity-Aware Proxy.** Shopify uses Google IAP. We need pluggable
   identity-aware access, starting with:
   - **Tailscale** (tsnet / Tailscale Serve / Funnel; identity from tailnet)
   - **Cloudflare Tunnel + Cloudflare Access** (identity via Cf-Access-Jwt-Assertion)
   Both should surface a normalized Identity API to hosted sites (name, email,
   handle, etc.), like Quick's identity API.

## What Quick is (from the Shopify engineering post)

- Drop a folder of HTML/assets, get a secure URL (`mysite.quick.shopify.io`)
  only employees can see. No frameworks, pipelines, or config.
- Architecture: GCS bucket per-folder sites, gcsfuse mounts bucket as a local
  filesystem, NGINX wildcard vhost maps subdomain -> folder, everything behind IAP.
- `quick deploy` = thin wrapper around gcloud rsync (FTP-feel).
- A single shared backend server gives every site zero-config client-side APIs:
  - Database (Firebase-style document collections: create/subscribe, no schemas)
  - File uploads
  - AI (LLM/image gen proxy; keys held server-side)
  - Data warehouse queries
  - Websockets (multiplayer/realtime)
  - Identity (who is the visitor: name, title, team, Slack handle)
- Philosophy: no permissions, no site owners — anyone can overwrite any site;
  small fixed feature set; embrace constraints; trusted internal network makes
  zero-config safe. 50k+ sites on one $200/mo VM; server moved from Node to Go.

## Deliverables requested

- **Workflow design**: what `quick init`, `quick deploy`, `quick serve` (etc.)
  look like end to end for a user deploying to an arbitrary rsync-reachable host.
- **High-level architecture**: components (CLI in C23/curspan, server, proxy
  adapters), data flow, directory layout on the host, subdomain routing,
  identity normalization layer, optional backend API service, and how the
  trust model changes when the IAP is Tailscale vs Cloudflare Access.
