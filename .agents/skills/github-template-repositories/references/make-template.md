# Make A Repository A Template

Open this when turning an existing GitHub repository into a template repository.

## Preconditions

- Confirm the target repository owner/name.
- Confirm the user wants the repository itself marked as a template.
- Confirm the actor has admin permission on the repository.
- Check for Git LFS-tracked files; GitHub template repositories cannot include
  files stored with Git LFS.
- Check the default branch contents, because default template generation copies
  the default branch unless the generator includes all branches.

## Manual UI Path

1. Open the repository on GitHub.
2. Go to repository Settings.
3. Select the Template repository setting.
4. Save or confirm if the UI requires it.

Verify after the change:

```bash
gh repo view OWNER/REPO --json nameWithOwner,isTemplate,visibility,defaultBranchRef
```

## API Path

Use the repository update endpoint only after explicit confirmation:

```bash
gh api -X PATCH repos/OWNER/REPO \
  -f is_template=true
```

Then verify `isTemplate` with `gh repo view` or `is_template` with REST.

## Readiness Checklist

- README explains the template's intended use.
- Placeholder names, package names, domains, and secrets are replaced with safe
  examples.
- CI workflows do not assume the template repository's exact owner/name unless
  that is intentional.
- Default branch is the branch most users should start from.
- Non-default branches are safe to expose if users choose all branches.
- Licenses and notices are appropriate for reuse.
- No Git LFS files are required for generated repositories.
