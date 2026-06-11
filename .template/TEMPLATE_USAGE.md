# Template Usage Guide

This guide covers creating a project from `sammyjoyce/curspan` and cleaning up the template-specific files.

## Create A Repository

### GitHub UI

1. Open the template repository on GitHub.
2. Select `Use this template`.
3. Choose an owner, repository name, and visibility.
4. Create the repository.

### GitHub CLI

```bash
gh repo create my-cli \
  --template sammyjoyce/curspan \
  --public \
  --clone
cd my-cli
```

Generated repositories have their own history. Use a fork instead if you need to contribute directly back to the template repository.

## Clean Up The Generated Project

### GitHub Actions

Run the `Template Cleanup` workflow from the generated repository's Actions tab. It asks for the project name, description, author, GitHub owner, and license, then commits the resulting starter files.

## CI Runner Selection

Generated repositories default to GitHub-hosted runners so the cleanup workflow and first CI run work without self-hosted infrastructure.

To use Namespace or another self-hosted fleet, set these repository or organization variables:

| Variable | Default | Namespace example |
| --- | --- | --- |
| `CI_LINUX_RUNNER` | `ubuntu-latest` | `nscloud-ubuntu-24.04-amd64-4x8` |
| `CI_MACOS_RUNNER` | `macos-latest` | `nscloud-macos-sequoia-arm64-6x14` |
| `CI_WINDOWS_RUNNER` | `windows-latest` | `nscloud-windows-2022-amd64-4x8` |

The workflows use variable expressions instead of literal custom runner labels, so local `actionlint` can validate the default template workflows without a custom allowlist.
The Namespace macOS example is ARM64; use the default `macos-latest` runner or add a universal/x64 release job if you need Intel macOS artifacts.

### Local Interactive Setup

```bash
bash ./.template/setup.sh
```

Interactive setup requires `gum`, `jq`, `sd`, `zig`, and `bash`.

### Local Non-Interactive Setup

```bash
PROJECT_NAME="My CLI" \
PROJECT_DESCRIPTION="A focused terminal tool." \
AUTHOR_NAME="Your Name" \
AUTHOR_EMAIL="you@example.com" \
GITHUB_USERNAME="yourusername" \
PROJECT_URL="https://github.com/yourusername/my-cli" \
PROJECT_LICENSE="MIT" \
bash ./.template/setup.sh --non-interactive --cleanup
```

### Preview Replacements

```bash
bash ./.template/replacer.sh --dry-run -v
```

## Template Variables

Variables are configured in `.template/template-vars.json`.

| Variable | Purpose | Example |
| --- | --- | --- |
| `PROJECT_NAME` | Human-readable project name | `My CLI` |
| `PROJECT_NAME_SNAKE` | C/config identifier form | `my_cli` |
| `PROJECT_NAME_KEBAB` | Binary, package, and URL form | `my-cli` |
| `PROJECT_DESCRIPTION` | README and metadata description | `A focused terminal tool.` |
| `AUTHOR_NAME` | Primary maintainer name | `Your Name` |
| `AUTHOR_EMAIL` | Maintainer email | `you@example.com` |
| `GITHUB_USERNAME` | GitHub owner or organization | `yourusername` |
| `PROJECT_URL` | Canonical repository URL | `https://github.com/yourusername/my-cli` |
| `CURRENT_YEAR` | Notice year | `2026` |
| `PROJECT_LICENSE` | SPDX-style license identifier | `MIT` |

The replacement script reads explicit environment variables first. When possible,
it falls back to local git metadata, the repository name, the current year, and
configured defaults.

Setup also refreshes `build.zig.zon` fingerprint after replacing the package
name. Zig 0.16 validates that package names and fingerprints match, so
generated repositories need a fresh fingerprint before they can build.

## What Cleanup Removes

Cleanup removes files that are useful only while instantiating the template:

- `.template/`
- `.templatesyncignore`
- `.github/template.yml`
- `.github/workflows/template-cleanup.yml`

The generated project keeps normal development files such as CI workflows, issue templates, documentation, examples, and source code.

## After Cleanup

Run these checks before first commit if you cleaned up locally:

```bash
zig build
zig build test
zig build terminal-test
zig build -Dterminal-backend=ghostty terminal-test  # require Ghostty VT when available
zig build -Dterminal-backend=none terminal-test                       # force non-PTY mode
zig build check
```

For the default TUI build, `zig build terminal-test` drives the menu through a pseudo-terminal only when `libghostty-vt` is available.
Use `-Dterminal-backend=ghostty` to require that backend instead of skipping PTY-backed TUI coverage on machines without Ghostty VT.
Use `nix develop` as a convenience path if you want the repository-provided Nix shell.

Also run the menu command in a real terminal before shipping major UI changes:

```bash
zig build run
```

Check startup, keyboard navigation, color fallback, resize behavior, and whether your shell prompt is restored after exit.

## Template Maintainer Notes

- Keep `.template/template-vars.json` as the source of truth for replacement variables.
- Prefer specific placeholders such as `myapp` or `https://github.com/yourusername/yourproject`; avoid short common words.
- Test replacements with `--dry-run -v` before changing broad file patterns.
- Keep generated-project docs in `.template/TEMPLATE_README.md`.
- Keep template-user docs in `.template/TEMPLATE_USAGE.md`.
