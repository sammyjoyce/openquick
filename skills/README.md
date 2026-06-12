# OpenQuick skill package

This directory contains the installable OpenQuick agent skill.
The skill teaches an agent how to set up, manage, and use OpenQuick: deploy static sites, operate a private host, and use the same-origin SDK.

## Install

Build the archive first if it is not present or is stale:

```bash
sh skills/build.sh
```

Install with a skills-compatible runtime:

```bash
npx skills add skills/openquick-deploy.skill
```

For Claude Code, place the unpacked skill in `~/.claude/skills/openquick`:

```bash
mkdir -p ~/.claude/skills/openquick
unzip -o skills/openquick-deploy.skill -d ~/.claude/skills/openquick
```

Manual install works for any runtime that reads a skill directory with `SKILL.md` at its root:

```bash
rm -rf /tmp/openquick-skill
mkdir -p /tmp/openquick-skill
unzip -o skills/openquick-deploy.skill -d /tmp/openquick-skill
```

Then point the runtime at `/tmp/openquick-skill` or copy that directory into its configured skills folder.

## Rebuild

Run the packaging script from the repository root:

```bash
sh skills/build.sh
unzip -l skills/openquick-deploy.skill
```

The archive contains `SKILL.md` plus every file under `skills/openquick-deploy/references/`.
