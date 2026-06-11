# Sources And Decisions

## Synthesis Summary

- Skill class: `integration-documentation`.
- Primary execution shape: `reference-backed-expert`.
- Secondary shape: fixed workflow routing by task branch.
- Simplicity rationale: inline-only guidance was rejected because GitHub
  template work has four distinct branches: making templates, generating
  repositories, CLI/API automation, and troubleshooting. Script-backed workflow
  was rejected because GitHub side effects require live auth, repository
  context, and confirmation rather than deterministic local automation.
- Portability note: the skill uses portable Agent Skills files. GitHub CLI and
  REST API commands are runtime options, not provider-specific skill mechanics.

## Source Inventory

| Source | Trust tier | Confidence | Contribution | Constraints |
| --- | --- | --- | --- | --- |
| https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-template-repository | Official GitHub Docs | High | Source for making an existing repo a template, admin permission, default branch behavior, optional all-branch inclusion, unrelated histories, and LFS exclusion. | UI wording can drift; verify live UI for exact click paths. |
| https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-repository-from-a-template | Official GitHub Docs | High | Source for read-access generation, template-vs-fork differences, Use this template flow, include-all-branches behavior, and contribution graph/history semantics. | UI wording can drift; verify live UI for exact click paths. |
| https://docs.github.com/en/rest/repos/repos?apiVersion=2022-11-28 | Official GitHub REST API Docs | High | Source for `is_template`, `POST /repos/{template_owner}/{template_repo}/generate`, token permissions, route/body parameters, and response validation. | API version and docs are time-sensitive; check current docs for exact fields when implementing. |
| https://cli.github.com/manual/gh_repo_create | Official GitHub CLI manual | High | Source for `gh repo create`, `--template`, `--include-all-branches`, visibility flags, cloning, and owner/name behavior. | CLI versions can drift; verify installed `gh repo create --help` when command behavior matters. |
| `skill-creator` | Local skill-authoring source | High | Required UI metadata, concise skill shape, one-level references, no README-style clutter, and validator use. | Skill-authoring guidance, not GitHub domain authority. |
| `skill-writer` | Local skill-authoring source | High | Required synthesis, source adaptation, reference architecture, description optimization, SPEC, and registration validation. | Skill-authoring guidance, not GitHub domain authority. |

## Source Adaptation Notes

| Decision | Record |
| --- | --- |
| Source intent | GitHub docs explain how to mark a repo as a template and how to generate new repos from templates. |
| Local target | Produce a reusable agent skill for safely planning, performing, and validating GitHub template repository work. |
| Fidelity boundary | Preserve GitHub permission, branch, LFS, history, and API semantics. |
| Local replacement | Convert UI documentation into agent decision rules, confirmation gates, API/CLI checks, and validation commands. |
| Omitted material | Screenshots, generic repository creation steps not specific to templates, and unrelated GitHub sidebar content. |
| Rights and attribution | Link to official docs and paraphrase; do not copy large page text. |

## Coverage Matrix

| Dimension | Coverage |
| --- | --- |
| API surface and behavior contracts | Template repository setting, `is_template`, REST generate endpoint, `gh repo create --template`, include-all-branches behavior, default branch behavior, visibility, and target owner/name fields. |
| Config/runtime options | GitHub UI, GitHub CLI, REST API, token scopes/permissions, owner selection, public/private/internal visibility, clone behavior, target org policy, and all-branch inclusion. |
| Common downstream use cases | Mark a starter repo as a template, generate a new project, generate into an org, clone generated repo, include all branches, audit readiness, choose template versus fork, and automate with REST. |
| Known issues/workarounds | Missing admin access, missing read access, org creation restrictions, `is_template` false, Git LFS files, wrong visibility, branch histories unrelated, all branches omitted, token permission failures, and confusing template-vs-fork expectations. |
| Version/platform variance | GitHub UI drift, GitHub CLI version drift, REST API versioning, GitHub Enterprise differences, organization policies, and private template access rules. |

## Decisions

| Status | Decision | Rationale |
| --- | --- | --- |
| Adopted | Skill root `.agents/skills/github-template-repositories/`. | The repo now uses `.agents/skills/`; the name is specific and does not collide with broader GitHub skills. |
| Adopted | Reference-backed expert layout. | The task branches are distinct enough to keep focused references. |
| Adopted | Require confirmation before GitHub side effects. | Marking templates and creating repos are external, persistent actions. |
| Adopted | Use official GitHub Docs and CLI manual as authoritative sources. | The user provided GitHub docs and the behavior is platform-owned. |
| Rejected | Add automation scripts. | Live GitHub auth and confirmation make scripts too side-effect-prone for this skill. |

## Description Optimization

Final description:

> Authoritative GitHub template repository guidance for creating, auditing, or generating repositories from templates. Use for GitHub template repo settings, Use this template flows, gh repo create --template, REST API /repos/{template_owner}/{template_repo}/generate, include_all_branches, is_template, template-vs-fork decisions, Git LFS exclusions, permissions, visibility, unrelated template histories, and template repository troubleshooting.

Should trigger:

- "Make this GitHub repo a template."
- "Create a new repo from this template with gh."
- "Use the REST API to generate from a template repo."
- "Should this be a fork or a template?"
- "Why can't I include all branches from this template?"

Should not trigger:

- "Create a pull request template file."
- "Add issue templates."
- "Fork this repo to contribute upstream."
- "Create a normal empty GitHub repo."
- "Scaffold local files without GitHub template behavior."

Edits made:

- Included exact trigger vocabulary for GitHub UI, CLI, REST, branch inclusion,
  `is_template`, permissions, LFS, and template-vs-fork choices.
- Excluded pull request and issue templates to reduce false positives.

## Gaps And Stopping Rationale

- Further GitHub docs retrieval would be low-yield for the initial skill; the
  core UI, CLI, REST, permissions, branch, LFS, and history behavior is covered.
- Exact live UI labels and installed `gh` behavior may drift; verify before
  performing a real side effect.

## Changelog

- 2026-05-26: Created initial GitHub template repository skill from official
  GitHub Docs, GitHub CLI manual, `skill-creator`, and `skill-writer`.
