# Tailscale modes for OpenQuick

Tailscale is the recommended private-network path for homelabs and small teams. OpenQuick supports three host modes.

## `tailscale-serve`

Simplest setup:

```bash
tailscale serve --bg https / http://127.0.0.1:9366
```

Flow:

```text
Browser on tailnet -> https://quick.<tailnet>.ts.net -> Tailscale Serve -> quickd localhost
```

Tailscale Serve can add `Tailscale-User-Login`, `Tailscale-User-Name`, and `Tailscale-User-Profile-Pic` for tailnet traffic. `quickd` may trust those headers only when the immediate peer is localhost and spoofed inbound identity headers are stripped by Serve.

Pure `*.ts.net` names are machine-name oriented, not arbitrary wildcard site subdomains. Use a path fallback such as:

```text
https://quick.<tailnet>.ts.net/~/<site>/
```

or add a custom wildcard domain.

## `tailscale-localapi`

Best for true private wildcard subdomains.

Flow:

```text
Browser on tailnet -> https://site.quick.example.com -> Caddy on Tailscale IP -> quickd localhost
```

Requirements:

- wildcard DNS for `*.quick.example.com` resolves to the host's Tailscale address for viewers;
- Caddy obtains a wildcard certificate, usually with DNS-01;
- Caddy proxies to `127.0.0.1:9366`;
- `quickd` trusts `X-Forwarded-For` only from configured local proxies;
- `quickd` calls Tailscale LocalAPI WhoIs for the real source Tailscale IP.

Example host IAP fragment:

```json
{
  "iap": {
    "type": "tailscale",
    "mode": "localapi",
    "trusted_proxies": ["127.0.0.1/32"],
    "source_ip_header": "X-Forwarded-For"
  }
}
```

Do not let this Caddy listener bind publicly unless another IAP is in front.

## `tailscale-tsnet`

Go-native mode where `quickd` joins the tailnet itself.

Flow:

```text
Browser on tailnet -> tsnet listener in quickd
```

`quickd` creates a `tsnet.Server`, accepts tailnet HTTP traffic, and identifies callers with Tailscale WhoIs. This avoids depending on the host `tailscaled` daemon for serving, but you must manage tsnet state and auth-key lifecycle. Pure `*.ts.net` wildcard caveats still apply; use path fallback or custom DNS for per-site subdomains.

Example host IAP fragment:

```json
{
  "iap": {
    "type": "tailscale-tsnet"
  }
}
```

`quickd` must be built with the `tsnet` build tag for this mode. By default it stores tsnet state under `<data_dir>/tsnet`, listens on the configured port inside the tailnet, and lets Tailscale read `TS_AUTHKEY` when first authorizing. Systemd installs read `/etc/openquick/quickd.env` when present, so host installs can set `TS_AUTHKEY` there without storing the secret in `quickd.json`. If no reusable state or auth key is available, tsnet may print an interactive auth URL in the service logs.

## Funnel warning

Tailscale Funnel exposes a service to the public internet. Funnel traffic does not carry tailnet user identity headers, so Funnel is not an OpenQuick identity boundary by itself.

Treat Funnel as:

- `iap.type = none` unless paired with another auth layer;
- suitable for intentional public previews;
- unsafe as the default trusted-user OpenQuick deployment mode.

`quick doctor` should warn when Funnel is enabled and identity would be anonymous.
