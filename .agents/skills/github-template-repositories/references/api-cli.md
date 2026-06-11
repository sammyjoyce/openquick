# API And CLI

Open this when using GitHub CLI, REST API, tokens, or automation for template
repositories.

## GitHub CLI

Create from a template:

```bash
gh repo create TARGET_OWNER/TARGET_REPO \
  --template TEMPLATE_OWNER/TEMPLATE_REPO \
  --public
```

Useful flags:

| Flag | Use |
| --- | --- |
| `--template OWNER/REPO` | Source template repository. |
| `--include-all-branches` | Copy files and directory structure from all template branches. |
| `--public`, `--private`, `--internal` | Target visibility. Use exactly one for non-interactive creation. |
| `--clone` | Clone the generated repo locally after creation. |
| `--description` | Set target repository description. |
| `--team` | Grant an organization team access when applicable. |

Verify installed CLI behavior before relying on flags in automation:

```bash
gh repo create --help
```

## REST Generate Endpoint

Endpoint:

```text
POST /repos/{template_owner}/{template_repo}/generate
```

Minimal body:

```json
{
  "owner": "TARGET_OWNER",
  "name": "TARGET_REPO",
  "private": true
}
```

Body fields commonly needed:

| Field | Use |
| --- | --- |
| `owner` | Target owner. Required when creating under an organization or non-default owner. |
| `name` | Required target repository name. |
| `description` | Optional target description. |
| `include_all_branches` | `true` to copy all branches; default is `false`. |
| `private` | `true` for private target; default is public. |

Example through `gh api`:

```bash
gh api -X POST repos/TEMPLATE_OWNER/TEMPLATE_REPO/generate \
  -f owner='TARGET_OWNER' \
  -f name='TARGET_REPO' \
  -F private=true \
  -F include_all_branches=false
```

## REST Permissions

- For a non-public template, the authenticated user must have read access to
  the template repository, subject to owner and organization
  repository-creation permissions.
- Classic tokens need `public_repo` or `repo` scope for public targets, and
  `repo` scope for private targets.
- Fine-grained tokens need repository Administration write and Contents read
  permissions for this operation.
- To check template eligibility, get repository metadata and confirm
  `is_template: true`.

## Automation Guardrails

- Never hard-code tokens; use `GH_TOKEN`, `GITHUB_TOKEN`, or a secret manager.
- Dry-run by reading source and target state before any POST/PATCH.
- Make the target owner/name explicit in logs.
- Treat `409`, `422`, and permission errors as blockers that need state
  inspection, not retry loops.
- Validate the returned repository JSON before proceeding to clone, configure,
  or push.
