# GitHub Template Repositories Specification

## Intent

This skill is the project-local authority for GitHub template repository work.
It guides agents that create template repositories, generate new repositories
from templates, choose template-vs-fork behavior, or automate template flows
through GitHub CLI or REST APIs.

## Scope

In scope:

- Marking existing GitHub repositories as templates.
- Creating new repositories from template repositories through GitHub UI, GitHub
  CLI, or REST API.
- Auditing template readiness, branch inclusion, unrelated history semantics,
  Git LFS exclusions, visibility, permissions, and access failures.
- Explaining when a template should be used instead of a fork or ordinary repo
  creation.

Out of scope:

- General GitHub repository administration unrelated to template behavior.
- Fork contribution workflows except as a comparison point.
- GitHub Classroom assignment setup beyond noting template compatibility.
- Storing tokens, secrets, customer data, or private repository details in skill
  artifacts.

## Users And Trigger Context

- Primary users: coding agents working on GitHub repository template tasks.
- Common requests: make this repo a template, create a repo from a template,
  automate template creation with `gh` or REST, include all branches, audit a
  template repo, or debug why a template cannot be used.
- Should not trigger for: ordinary PR work, non-template repo creation, forks
  used for contribution, issue templates, pull request templates, or local code
  scaffolding with no GitHub template repository behavior.

## Runtime Contract

- Required first actions: classify make-template versus generate-from-template,
  identify source and target repositories, and check permissions/current state.
- Required outputs: action taken or exact blocker, validation result, and any
  side effects performed.
- Non-negotiable constraints: confirm before external GitHub side effects, do
  not expose tokens, do not claim generated repos can PR/merge back to template
  histories, and do not put Git LFS files in template repositories.
- Expected runtime files: `SKILL.md` plus the focused reference matching the
  task branch.

## Source And Evidence Model

Authoritative sources:

- GitHub Docs for template repositories and creating from a template.
- GitHub REST API docs for repository template generation and repository
  `is_template` metadata.
- GitHub CLI manual for `gh repo create --template`.

Useful improvement sources:

- Positive examples: successful template publication and generated-repo
  validation.
- Negative examples: permission failures, LFS blockers, wrong branch inclusion,
  visibility mismatch, and template-vs-fork misuse.
- Validation results: `gh repo view`, REST responses, and repository UI/API
  state after changes.

Data that must not be stored:

- Tokens or secret values.
- Private repository names unless necessary for the current task result.
- Customer data.

## Reference Architecture

- `SKILL.md` contains the authority boundary, routing, core rules, preferred
  paths, and validation contract.
- `references/make-template.md` covers turning a repository into a template.
- `references/generate-from-template.md` covers creating new repositories from
  templates and choosing template versus fork.
- `references/api-cli.md` covers GitHub CLI and REST API execution details.
- `references/troubleshooting.md` covers permission, LFS, branch, history,
  visibility, and validation failures.
- `agents/openai.yaml` contains skill-creator UI metadata.
- `SOURCES.md` contains provenance, decisions, coverage, trigger optimization,
  and changelog.

## Validation

- Lightweight validation: run both skill validators and `git diff --check`.
- Runtime validation for future GitHub tasks: verify `is_template`, target
  visibility, target default branch, optional all-branch behavior, and template
  repository linkage after any creation.
- Acceptance gates: valid frontmatter, skill directory name matches `name`, all
  referenced files resolve, references are one level deep, and side-effect
  confirmation is explicit.

## Known Limitations

- GitHub organization policies can restrict who may create repositories or
  choose visibility.
- UI wording and placement can drift; verify current docs or live UI when exact
  click paths matter.
- Real GitHub side effects cannot be proven by local validation alone.

## Maintenance Notes

- Update `SKILL.md` when trigger scope, core behavior, or side-effect policy
  changes.
- Update references when GitHub CLI or REST parameters change.
- Update `SOURCES.md` when official docs or source decisions change.
