---
name: github-template-repositories
description: >
  Authoritative GitHub template repository guidance for creating, auditing, or
  generating repositories from templates. Use for GitHub template repo settings,
  Use this template flows, gh repo create --template, REST API
  /repos/{template_owner}/{template_repo}/generate, include_all_branches,
  is_template, template-vs-fork decisions, Git LFS exclusions, permissions,
  visibility, unrelated template histories, and template repository
  troubleshooting.
---

# GitHub Template Repositories

Use this skill as the authority for GitHub template repository behavior. Treat
GitHub's current docs as source of truth, then apply local repo or organization
policy on top.

Maintenance scope is in `SPEC.md`; source provenance and decisions are in
`SOURCES.md`.

## First Moves

1. Classify the task:
   - read `references/make-template.md` when making an existing repo available
     as a template;
   - read `references/generate-from-template.md` when creating a new repo from
     a template;
   - read `references/api-cli.md` when using `gh`, REST, tokens, or automation;
   - read `references/troubleshooting.md` when access, branch, history, LFS,
     visibility, or permission behavior is unclear.
2. Identify the source template repository, target owner, target repository
   name, visibility, and whether all branches should be included.
3. Confirm before any GitHub side effect: changing `is_template`, creating a
   repository, publishing a generated repository, or changing visibility.
4. Verify access and current state before acting. For API or CLI paths, check
   that the template repo has `is_template: true`.
5. After acting, verify the target repository exists and records the expected
   template relationship, branch set, visibility, and clone URL.

## Core Rules

- Admin permission is required to make a repository a template.
- Read access to a template repository is enough to create a repository from
  it, subject to owner and organization repository-creation permissions.
- Template generation is not for contributing back. Generated repositories have
  unrelated histories, so template branches cannot be merged or PR'd back to the
  template branch lineage.
- Default generation copies the default branch directory structure and files.
  Include all branches only when the target should inherit branch names and
  files from every template branch.
- Template repositories cannot include files stored with Git LFS.
- Choose a template when starting a new project from starter files. Choose a
  fork when preserving history and contributing back matters.
- Do not paste tokens into logs, skills, docs, or PR descriptions. Use env vars
  or redacted placeholders.

## Preferred Paths

| Need | Preferred path |
| --- | --- |
| Mark repo as template manually | Repository Settings -> Template repository checkbox. |
| Mark repo as template programmatically | Patch repository metadata with `is_template: true`, after confirming admin access. |
| Create from template manually | Use this template -> Create a new repository. |
| Create from template with GitHub CLI | `gh repo create OWNER/REPO --template TEMPLATE_OWNER/TEMPLATE_REPO --public|--private|--internal`. |
| Create from template with REST | `POST /repos/{template_owner}/{template_repo}/generate`. |
| Include every branch | Use `--include-all-branches` or `include_all_branches: true` deliberately. |
| Audit template readiness | Check `is_template`, LFS usage, default branch contents, docs placeholders, licenses, secrets, and CI assumptions. |

## Validation

Run the narrowest checks that prove the requested outcome:

```bash
gh repo view TEMPLATE_OWNER/TEMPLATE_REPO --json nameWithOwner,isTemplate,visibility,defaultBranchRef
gh repo view TARGET_OWNER/TARGET_REPO --json nameWithOwner,visibility,defaultBranchRef,templateRepository
```

For API workflows, also inspect the REST response status and returned repository
JSON. If permissions, private repository access, or organization policy block
the action, report the exact failing prerequisite instead of retrying blindly.
