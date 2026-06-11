# Generate From A Template

Open this when creating a new GitHub repository from a template repository.

## Preconditions

- Confirm the source template as `TEMPLATE_OWNER/TEMPLATE_REPO`.
- Confirm the target owner, target repository name, visibility, and description.
- Confirm whether to include only the default branch or all branches.
- Confirm before creating the target repository.
- Verify the template is usable:

```bash
gh repo view TEMPLATE_OWNER/TEMPLATE_REPO --json nameWithOwner,isTemplate,visibility,defaultBranchRef
```

If `isTemplate` is false, stop and either mark it as a template or choose a
different source.

## GitHub UI Path

1. Open the template repository.
2. Choose `Use this template`.
3. Choose `Create a new repository`.
4. Select owner, name, description, and visibility.
5. Select `Include all branches` only when the target should copy every branch
   from the template.
6. Create the repository.

## GitHub CLI Path

```bash
gh repo create TARGET_OWNER/TARGET_REPO \
  --template TEMPLATE_OWNER/TEMPLATE_REPO \
  --private
```

Use exactly one visibility flag: `--public`, `--private`, or `--internal`.

Add `--include-all-branches` only when needed:

```bash
gh repo create TARGET_OWNER/TARGET_REPO \
  --template TEMPLATE_OWNER/TEMPLATE_REPO \
  --include-all-branches \
  --private \
  --clone
```

## Template Versus Fork

Choose a template when:

- the new repository starts a new project;
- preserving old commit history is unwanted;
- users should get starter files and then diverge;
- generated commits should count in the new owner's contribution graph.

Choose a fork when:

- preserving full source history matters;
- the user intends to contribute changes back upstream;
- pull requests back to the parent repository are part of the workflow.

Generated repositories start with unrelated histories. Do not promise merges or
pull requests between generated branches and template branches.

## Post-Create Validation

```bash
gh repo view TARGET_OWNER/TARGET_REPO --json nameWithOwner,visibility,defaultBranchRef,templateRepository
gh repo clone TARGET_OWNER/TARGET_REPO
```

If all branches were requested, inspect branch names after creation:

```bash
gh repo clone TARGET_OWNER/TARGET_REPO
git -C TARGET_REPO branch -a
```
