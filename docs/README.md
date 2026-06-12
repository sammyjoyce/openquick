# Documentation

Developer and user documentation for OpenQuick.

## Product and workflow

| Guide | Read it when you want to… |
| --- | --- |
| [Design architecture](design/ARCHITECTURE.md) | Understand the authoritative CLI/server/SDK split and v0 boundaries |
| [UnJS SDK assessment](design/UNJS_ASSESSMENT.md) | Review UnJS package and unpkg suitability for the browser SDK |
| [Workflow design](design/WORKFLOW.md) | Understand install, init, deploy, open, list, and doctor behavior |
| [User guide: quick init](user/quick-init.md) | Scaffold a static OpenQuick site |
| [User guide: quick deploy](user/quick-deploy.md) | Deploy with rsync over SSH and quickd activation |
| [User guide: SDK](user/sdk.md) | Use `/_quick/sdk.js` from hosted sites |
| [User guide: AI runtime](user/ai.md) | Configure and call the config-gated AI proxy |
| [User guide: Warehouse queries](user/warehouse.md) | Configure and call named warehouse queries |
| [User guide: TUI](user/tui.md) | Use the interactive OpenQuick dashboard |
| [Cloudflare IAP](user/iap-cloudflare.md) | Configure Cloudflare Access in front of quickd |
| [Tailscale IAP](user/iap-tailscale.md) | Configure Tailscale Serve/localapi/tsnet modes |

## Developer internals

| Guide | Read it when you want to… |
| --- | --- |
| [Architecture overview](ARCHITECTURE.md) | Navigate the current repo layout and build surfaces |
| [Public contracts](CONTRACTS.md) | Know which CLI and internal UI seams are stable |
| [Components](COMPONENTS.md) | Work on the vendored `cs_` rendering components used by CLI output |
| [Theming](THEMING.md) | Work on terminal color roles and degradation |
| [Zig primer](ZIG_PRIMER.md) | Drive the Zig build system with no prior Zig experience |
| [Testing](TESTING.md) | Run the CLI contract and PTY/TUI scenario tests |

## Elsewhere in the repo

- [README](../README.md) - project overview and quick start
- [CONTRIBUTING](../CONTRIBUTING.md) - workflow, branch, and commit conventions
- [examples/sites/](../examples/sites/) - static site examples
