# Troubleshooting

Open this when GitHub template repository behavior is blocked, surprising, or
inconsistent.

## Quick Checks

```bash
gh auth status
gh repo view TEMPLATE_OWNER/TEMPLATE_REPO --json nameWithOwner,isTemplate,visibility,defaultBranchRef
gh repo view TARGET_OWNER/TARGET_REPO --json nameWithOwner,visibility,defaultBranchRef,templateRepository
```

For REST paths, inspect status code, response body, and token permissions.

## Failure Matrix

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Template option is missing | Repository is not marked as a template or actor lacks access | Verify `is_template`/`isTemplate` and access. |
| Cannot mark repo as template | Actor lacks admin permission | Use an admin account or ask the repo owner. |
| Generate endpoint says template not found | Private template access or owner/repo typo | Verify source repo visibility and authenticated user's membership. |
| Generated repo lacks non-default branches | All-branch copy was not selected | Use `--include-all-branches` or `include_all_branches: true`. |
| Cannot PR back to template | Generated branches have unrelated histories | Use a fork if contribution-back is required. |
| Git LFS assets missing or blocked | Template repositories cannot include LFS files | Remove LFS dependency or store assets elsewhere. |
| API token creates public but not private repo | Token lacks private repo scope/permission | Use `repo` classic scope or fine-grained Administration write plus Contents read. |
| Organization target fails | Org policy or membership blocks repo creation | Check org repository creation permissions and target owner. |
| Visibility is wrong | Missing or wrong visibility/private flag | Recreate correctly or change visibility if policy permits. |
| Local clone points at template | User cloned template instead of generated target | Clone the target repository URL after generation. |

## Branch And History Diagnostics

- A generated repository is not a fork.
- It starts a new project lineage and does not preserve full template commit
  history like a fork.
- Branches copied from templates have unrelated histories. Avoid merge plans
  that assume a shared base.
- If all branches matter, validate branch names immediately after generation.

## Readiness Audit

Before publishing a template, check:

1. No secret values, internal hostnames, or private URLs in files.
2. No Git LFS-backed files required for starter behavior.
3. README states how to instantiate and rename project placeholders.
4. CI workflows either work in generated repos or clearly document required
   secrets and permissions.
5. Package metadata does not publish under the template's own package name by
   accident.
6. Licenses and notices permit reuse.
7. The default branch is the intended starter branch.
