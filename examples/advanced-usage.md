# Advanced Usage

How to script and integrate the `quick` CLI for OpenQuick workflows.

- [Output formats and streams](#output-formats-and-streams)
- [Parsing output with jq](#parsing-output-with-jq)
- [Exit codes in scripts](#exit-codes-in-scripts)
- [Diagnostics](#diagnostics)
- [Deployment planning](#deployment-planning)
- [Configuration and environment](#configuration-and-environment)
- [Integration patterns](#integration-patterns)

## Output formats and streams

The CLI keeps a stable I/O contract: results go to **stdout**, errors and
diagnostics go to **stderr**, and non-TTY stdout defaults to machine-readable
output where a command supports it. Use `--plain` when a pipeline needs human
text.

```bash
quick info                       # human-readable
quick --json info                # machine-readable
quick --plain info               # no colors, good for logs
NO_COLOR=1 quick doctor          # same color policy via environment

quick info > info.txt            # capture stdout
quick doctor 2> diagnostics.log  # capture stderr only
quick info > out.txt 2>&1        # capture both
```

Use `--quiet`, `--verbose`, or `--debug` to dial verbosity up or down.

## Parsing output with jq

`quick opencli` prints the whole CLI as JSON with a stable schema, which makes it
useful for discovery and assertions in scripts.

```bash
quick opencli | jq -r '.command.commands[].name'
quick opencli | jq -r '.command.exitCodes[] | "\(.code)\t\(.description)"'
quick opencli | jq -r '.command.options[].name'
quick opencli | jq -r '.info.version'
```

Command output works the same way:

```bash
quick --json info | jq .
quick info | jq .                         # JSON because stdout is a pipe
quick --json doctor | jq '.checks[] | [.group, .name, .status]'
```

## Exit codes in scripts

Commands return categorized exit codes: `0` for success, `2` for an unknown
command, `6` for a missing argument, `7` for an unknown option, and so on. The
full list is in the contract:

```bash
quick opencli | jq -r '.command.exitCodes[] | "\(.code) \(.description)"'
```

Branch on them like any Unix tool:

```bash
set -euo pipefail
quick --json info > build-info.json

quick frobnicate || echo "exit code: $?"        # unknown command -> 2
quick --unknown-option || echo "exit code: $?"  # unknown option -> 7
```

## Diagnostics

`quick doctor` reports local, remote, and edge/IAP status. Use `--json` for
automation.

```bash
quick doctor
quick --json doctor
quick doctor --remote --profile lab
```

For a hard CI gate, parse the JSON checks and fail on statuses that matter for
your deployment profile.

## Deployment planning

`quick deploy --dry-run` resolves the site, profile, output path, remote root,
and URL without transferring files.

```bash
quick init /tmp/lunch-vote --name lunch-vote --profile lab
cd /tmp/lunch-vote
quick deploy --dry-run --json | jq .
quick open --plain
```

Use dry runs before enabling real `rsync` transfer in automation.

## Configuration and environment

Resolution precedence is **CLI flags > environment > quick.json > user config >
defaults**.

- User config: `~/.config/openquick/config.json`, or an explicit path via
  `--config` / `QUICK_CONFIG_PATH`.
- Site config: committed `quick.json` in the site directory.
- Common environment: `QUICK_PROFILE`, `QUICK_SITE`, `QUICK_REMOTE`,
  `QUICK_BASE_DOMAIN`, `QUICK_LOG_LEVEL`, `NO_COLOR`.

```bash
QUICK_PROFILE=lab quick deploy --dry-run
QUICK_SITE=demo QUICK_BASE_DOMAIN=quick.example.com quick open --plain
QUICK_LOG_LEVEL=DEBUG quick doctor --verbose
```

## Integration patterns

### Container smoke command

```dockerfile
FROM debian:stable-slim
COPY zig-out/bin/quick /usr/local/bin/quick
ENTRYPOINT ["quick"]
CMD ["--help"]
```

### systemd timer

```ini
# /etc/systemd/system/openquick-health.service
[Unit]
Description=OpenQuick health check

[Service]
Type=oneshot
ExecStart=/usr/local/bin/quick doctor --json
```

```ini
# /etc/systemd/system/openquick-health.timer
[Unit]
Description=Run the OpenQuick health check hourly

[Timer]
OnCalendar=hourly
Persistent=true

[Install]
WantedBy=timers.target
```

### CI contract check

Assert the binary's contract still matches the checked-in spec, the same
invariant `zig build test` enforces:

```bash
diff <(quick opencli) opencli.json
```

## See also

- [Public contracts](../docs/CONTRACTS.md)
- [User deploy guide](../docs/user/quick-deploy.md)
- [Documentation index](../docs/README.md)
