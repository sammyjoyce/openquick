# Template Support

This page is for problems with the **template itself**, not with a project you generated from it.

- **Search existing issues:** [curspan/issues](https://github.com/sammyjoyce/curspan/issues)
- **Open a new issue:** use the provided issue templates.
- **Problems in your generated project:** use that repository's own issue tracker.

## Quick fixes

**Cleanup did not run.** Check the **Template Cleanup** run in the Actions tab, then run it locally:

```bash
./.template/setup.sh                              # interactive
./.template/setup.sh --non-interactive --cleanup  # no prompts
```

**Placeholders are still visible.** Setup installs a fresh project README and runs `replacer.sh`, which swaps the template placeholders for your values:

| Placeholder | Becomes |
| --- | --- |
| `myapp` (and `yourproject`) | your project's kebab-case name |
| `https://github.com/yourusername/yourproject` | your GitHub owner and repository |
| `Your Name` | the configured author name |
| `you@example.com` | the configured author email |
| `~/.config/myapp` | `~/.config/<your-project-kebab>` |

If some are still present, re-run setup, or preview what would change with `./.template/replacer.sh --dry-run -v`.
