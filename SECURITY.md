# Security Policy

## Reporting a vulnerability

Please do not report security vulnerabilities through public GitHub issues.

Use **GitHub Private Vulnerability Reporting** instead:

1. Open the **Security** tab of the repository.
2. Click **Report a vulnerability**.
3. Include:
   - what the issue is (memory safety, command injection, supply chain, denial of service, …);
   - the affected file paths and the tag, branch, or commit;
   - reproduction steps and any required configuration;
   - a proof-of-concept if you have one;
   - the impact you see.

You will get an acknowledgement within 48 hours and an initial assessment within 7
days. We will keep you updated, work with you on disclosure timing, and credit you in
the advisory unless you prefer to remain anonymous.

## Supported versions

This template is pre-1.0; security fixes land on `main` and ship in the next tagged release. The latest release is the supported one.

## What the template does already

The repository ships these baseline defenses; review them and tighten if your threat model warrants:

- **Secret-zeroing helper.** `src/utils/memory.h` exports `app_secret_zero()` for
  clearing sensitive buffers. The template ships it as a primitive but no production
  path calls it yet; invoke it where your code holds secrets. Memory locking
  (`mlock` / `VirtualLock`) is not wired in either.
- **Static analysis in CI**. `clang-tidy` and `cppcheck` run on every change.
- **Supply chain in CI**. Gitleaks secret scanning, OpenSSF Scorecard, SBOM generation, and pinned GitHub Actions versions.
- **Compiler warnings.** C sources compile with `-Wall -Wextra -std=c23`. `-Doptimize=` selects the optimization level; no Zig runtime is linked into the C-only binary.

Compiler hardening (`-fstack-protector-strong`, `_FORTIFY_SOURCE=2`, PIE/RELRO) is
**not** enabled by default. Add the flags to `base_flags` in `build.zig` if you need
them. See [docs/ARCHITECTURE.md#security-model](docs/ARCHITECTURE.md#security-model)
for the full security model.

## Practices when adopting the template

- Keep dependencies current; Dependabot is preconfigured.
- Never commit secrets. Gitleaks runs in CI, and the pre-commit configuration also catches large files and broken YAML.
- Treat the config file (`~/.config/<name>/config.json`) as user-private; prefer environment variables for runtime secrets.
- Enable GitHub's code scanning and Dependabot alerts on your generated repository.

## Contact

GitHub Private Vulnerability Reporting (above) is the primary channel. For non-security maintainer contact, see [CODEOWNERS](.github/CODEOWNERS).
