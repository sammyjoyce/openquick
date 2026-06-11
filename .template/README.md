# Template Files

This directory holds the variable-replacement system that turns the template into a fresh project. It is removed from generated repositories during cleanup.

## Contents

| File | Purpose |
| --- | --- |
| `template-vars.json` | Source of truth for every template variable, its placeholders, and validation |
| `replacer.sh` | Replacement engine: detects values, applies case transforms, swaps placeholders |
| `setup.sh` | Interactive or non-interactive setup that runs the replacer and optional cleanup |
| `TEMPLATE_README.md` | The README installed into the generated project |
| `TEMPLATE_USAGE.md` | The full "using this template" guide |
| `TEMPLATE_SUPPORT.md` | Support notes for template (not generated-project) issues |
| `README.md` | This file |

## How replacement works

- **Value detection.** Each variable reads explicit environment values first, then falls back to its configured sources (git metadata, repository name, current year), then to a default.
- **Case transforms.** `replacer.sh` derives the forms it needs: `snake_case`, `kebab_case`, `pascal_case`, `lower_case`, and `upper_case`.
- **Validation.** Each variable carries a regex so a bad value fails fast.
- **Safety.** `--dry-run` previews every change, and excluded paths (`.git`, `.template`, `zig-out`, `.zig-cache`, vendored code) are never touched.

## For new projects

When you create a project from this template:

1. Run the **Template Cleanup** workflow from the Actions tab, or run setup locally.
2. Setup installs `TEMPLATE_README.md` as the project README.
3. Setup runs `replacer.sh` to substitute every variable.
4. Template-only files are removed when cleanup is requested.

If automatic cleanup did not run:

```bash
# Interactive
./.template/setup.sh

# Non-interactive, and remove template-only files
./.template/setup.sh --non-interactive --cleanup
```

Setup needs `jq`, `sd`, and `zig`. Interactive setup also needs `gum`.

## For template maintainers

Add or change variables in `template-vars.json`. Each entry uses this shape:

```json
{
  "variables": {
    "NEW_VAR": {
      "placeholders": ["OLD_TEXT"],
      "description": "What this variable represents",
      "sources": ["repository_name"],
      "transform": "kebab_case",
      "validation": "^[a-z][a-z0-9-]*$"
    }
  }
}
```

`sources` is an ordered list; the first one that resolves wins. Available sources:

| Source | Resolves to |
| --- | --- |
| `repository_name` | The repository name (from Git or GitHub) |
| `repository_description` | The repository description |
| `repository_owner` | The GitHub owner or organization |
| `repository_url` | The GitHub repository URL |
| `git_user_name` | `git config user.name` |
| `git_user_email` | `git config user.email` |
| `current_year` | The current calendar year |
| `license` | The detected license, where supported |

Preview changes before committing them:

```bash
./.template/replacer.sh --dry-run -v
```

Prefer specific placeholders such as `myapp` or `https://github.com/yourusername/yourproject` over short common words, so replacement never touches unrelated text.
