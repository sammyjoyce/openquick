# Interactive TUI

The OpenQuick TUI is the same product workflow as the CLI, presented as a keyboard-driven dashboard.

## Launching

```bash
quick          # with no arguments on an interactive terminal
quick menu     # hidden command; opens the same dashboard
```

A bare `quick` starts the TUI only on an interactive terminal. If JSON output is enabled, the TUI refuses to start because machine JSON output and a full-screen interface share the same terminal stream. Default builds include the TUI; `zig build -Denable-tui=false` omits it.

The dashboard subtitle shows the active default profile and the profile config path, for example:

```text
profile lab · /home/alice/.config/openquick/config.json
```

## Keyboard conventions

Menus and panels use these conventions unless a screen footer says otherwise:

- `Up`/`Down` or `j`/`k` moves the selection.
- `PgUp`/`PgDn`, `Home`, and `End` move by page or to the first/last item.
- `1`-`9` jumps to the numbered visible row when numbers are shown.
- `Enter` selects the highlighted item.
- `/` opens incremental search on menus that enable search. Type to filter, `Backspace` deletes, `Esc` closes search, and `Enter` selects the current filtered item.
- Highlighted mnemonics come from `&` in menu labels. A unique mnemonic selects that item immediately; duplicate mnemonics cycle the selection and require `Enter`.
- `Esc` normally cancels the current menu, closes a panel, or goes back. On the main dashboard, `Esc` opens the compact menu with key bindings, About, and Exit.
- `q` acts like back/quit in menus and panels.
- `Ctrl-C` sends an interrupt. Menus exit, and a deploy in progress receives the same interrupt flag so it can report cancellation.

Input dialogs accept printable ASCII, `Backspace`, `Enter` to submit, and `Esc` to cancel. Confirm dialogs use `y` for yes and `n`, `q`, or `Esc` for no.

## Dashboard and Help/About

The main dashboard contains six workflow areas plus Help/About and Exit:

1. Sites
2. Deploy
3. New site
4. Doctor
5. Serve
6. Settings

Use `Help/About` for the built-in key binding summary or version/about text. The `Exit` item asks for confirmation before returning to the shell.

## Sites

The Sites screen reloads profiles, resolves the current plan, reads local deployment state, and, when the default profile has an SSH target, asks the host for remote rows. The list subtitle is:

```text
local rows first; remote rows when a profile has ssh
```

Rows are formatted as:

```text
<name> - <url> (<release>, <updated>) [stale]
```

`[stale]` appears on a local cached row when a remote refresh was requested but did not succeed.

Select a row to open Site details. The detail panel shows:

- `name`
- `url`
- `release`
- `updated`
- `deployer`
- `source` (`local` or `remote`)
- `stale` (`yes` or `no`)

Detail keys:

- `o`: open the site URL in the browser.
- `c`: copy the URL to the clipboard.
- `d`: start Deploy for this site name.
- `r`: return to the Sites list and refresh.
- `Esc`/`q`: return without action.

If no sites are known, the screen tells you to use New site and then Deploy.

## Deploy

Deploy is a plan -> progress -> result flow.

### 1. Resolve the plan

Deploy reloads profiles first. If you launched Deploy from the dashboard, and the current directory does not contain `quick.json`, it asks for a site directory path; blank means the current directory. Deploys launched from Sites or the New site result pass a site name override instead of showing that path prompt.

Next, choose a profile. When no profiles are configured, the fallback profile name is `local`. The TUI resolves a preliminary plan, then prompts for:

- Site name
- Subdomain

Blank input keeps the current default in each prompt. Values are normalized to DNS labels and must pass slug validation. After the prompts, the TUI resolves the final deploy plan.

### 2. Confirm the plan

The Deploy plan panel shows:

- site
- profile
- host
- url
- source
- output
- remote root
- rsync mode (`archive+compress, delete enabled, safe-links, chmod group-writable`)

Keys:

- `Enter`: run the deploy.
- `Esc`/`q`: cancel before any deploy work starts.

### 3. Watch progress and cancel if needed

Progress reports the shared deploy phases:

- build
- bootstrap check
- prepare
- transfer
- activate
- record

Use `Ctrl-C` to cancel while progress is running. There is no Esc key handler during the progress bar; cancellation comes from the interrupt signal passed into the deploy operation. A cancelled deploy reports:

```text
Deploy was cancelled before completion.
```

### 4. Read the result

A successful deploy opens a result panel with:

- release
- url
- changed
- reused
- deleted

Keys:

- `o`: open the deployed URL.
- `Enter`, `Esc`, or `q`: close the result.

A failure panel shows the failed phase, the failure message, and remediation. When the deploy result includes a bootstrap install command, that command is shown as the remediation; otherwise the panel tells you to fix the reported issue and run Deploy again.

## New site

New site is a wizard over `quick init`.

1. Enter the directory to create. Blank means the current directory.
2. Enter the site name. If blank, the directory value is used. The name is normalized to a DNS label; if an entered name changes during normalization, the TUI shows the normalized value before continuing.
3. Choose a template:
   - Blank: static HTML starter.
   - Realtime: starter page that imports `/_quick/sdk.js`.
4. Choose a default profile, or `None` to leave `quick.json` without a profile. If no profiles are configured, the wizard uses `None`.

On success, the result panel shows the created site, target path, and files created.

Keys on the result panel:

- `d`: start Deploy using the created site name.
- `Enter`, `Esc`, or `q`: close the panel.

## Doctor

Doctor starts with a scope menu:

- Local only: local CLI, tools, `quick.json`, and output checks.
- With remote: local checks plus selected host `quickd` checks.
- Deep: remote checks plus temporary deploy/probes.
- Back: return to the dashboard.

Remote and deep scopes select a profile when more than one profile exists; otherwise they use the default profile name. The progress panel reports checks running and then closes.

Doctor results are a searchable menu. Each row is:

```text
<icon> <group>/<name> - <detail>
```

Icons map to check status:

- `✓`: ok
- `!`: warn
- `✗`: fail
- `-`: skip

The subtitle shows the overall result. Press `Enter` on a check to open its detail panel. Detail fields are `group`, `status`, `name`, `detail`, and `remediation`. `Enter`, `Esc`, or `q` closes the detail panel.

## Serve

Serve has two actions: Local dev server and Host install guide.

### Local dev server

If a dev server child is already running, the status panel opens immediately. Otherwise the TUI prompts for:

- Port; blank uses `9366`.
- Identity email; blank leaves it unset.

The TUI asks the shared ops layer to build the `quickd --dev` command, forks it on supported POSIX builds, stores the child PID, and shows:

```text
running on http://localhost:<port> (pid <pid>)
```

Status keys while running:

- `s`: stop the child process.
- `o`: open the local URL.
- `Esc`, `q`, or `Enter`: close the status panel.

Stopping sends `SIGTERM`, waits briefly, and uses `SIGKILL` if the child does not exit. When no child is running, the status panel says the local dev server is stopped and only offers back/close.

On unsupported platforms, starting the local dev server shows a message instead of spawning a child.

### Host install guide

The install guide is read-only; it does not execute host changes. It prompts for:

- Profile; default is the current default profile.
- SSH host; default comes from the profile or `quick@host`.
- Remote root; default comes from the profile or `/srv/quick`.
- Base domain; default comes from the profile or `localhost`.
- IAP type; default comes from the profile or `tailscale`.

Values are validated for safe profile names, SSH targets, remote paths, domains, and IAP names. The guide then shows the command to run when ready:

```bash
quick serve install --profile <profile> --host <host> --remote-root <root> --domain <domain> --iap <iap> --execute
```

It also lists generated step summaries. The guide panel scrolls with `Up`/`Down` and `PgUp`/`PgDn`; `Esc`, `q`, or `Enter` closes it.

## Settings

Settings edits profiles and site config in memory first. Writes are explicit.

### Profiles

Profiles opens a searchable list of configured profiles plus `New profile` and Back. Each profile row shows the profile name, whether it is the default, its SSH target or `local/no-ssh`, and its base domain when present.

`New profile` prompts for a profile name and validates it before creating an in-memory profile.

The Profile panel shows:

- name
- ssh
- remote_root
- base_domain
- base_url
- iap.type
- iap.mode
- iap.team_domain
- iap.audience
- deploy.delete
- deploy.open_after_deploy

Profile keys:

- `e`: choose and edit a field.
- `s`: set this profile as the in-memory default.
- `w`: confirm and write the profile config file to disk.
- `Esc`/`q`: return.

Editable profile fields are `ssh`, `remote_root`, `base_domain`, `base_url`, `iap.type`, `iap.mode`, `iap.team_domain`, `iap.audience`, `deploy.delete`, and `deploy.open_after_deploy`. String fields use blank input to clear the field. Boolean deploy fields accept `true`/`false` or `1`/`0`; blank leaves the current boolean unchanged.

Write before leaving the profile panel if you want changes to persist. The Profiles list reloads profile config from disk when it is shown again.

### Site config

Site config first tries to load `quick.json` from the current directory. If that fails, it prompts for a path to `quick.json`.

The editable fields are:

- name
- subdomain
- source
- output
- build
- profile

Selecting a field opens an input dialog. `name` and `subdomain` are normalized to DNS labels when non-empty. `profile` must be a safe profile name when non-empty. Blank `source` or `output` becomes `.`; blank `build` or `profile` clears the field.

Choose `Write quick.json` to validate and write. The write step requires a valid site name and, when present, a valid subdomain, then asks for confirmation.

## State and file loading

Profiles are loaded through the shared profile config loader. The default path is:

- POSIX with `XDG_CONFIG_HOME`: `$XDG_CONFIG_HOME/openquick/config.json`
- POSIX without `XDG_CONFIG_HOME`: `~/.config/openquick/config.json`
- Windows: `%USERPROFILE%\AppData\Local\openquick\config.json`

A missing profile config is allowed. If the loaded config does not name a default profile, the TUI uses `local`.

The dashboard reloads profiles before each render, and screens reload at entry. Settings/Profile edits are therefore in-memory until you press `w` to write them.

Local deployment state lives under the site root:

```text
.quick/deployments/<profile>.json
```

A successful deploy writes `profile`, `site`, `url`, `release`, and `deployed_at`. Sites reads this local record for the resolved site root/profile and also reads remote rows when SSH is configured. `.quick/` is local state; generated `.quickignore` files exclude it from deploy transfers.
