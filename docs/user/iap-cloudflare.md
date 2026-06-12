# Cloudflare Access for OpenQuick

Use Cloudflare Tunnel plus Cloudflare Access when you want public DNS names protected by an organization login and no inbound ports on the origin.

## Target topology

```text
Browser -> Cloudflare Access -> Cloudflare Tunnel -> quickd on 127.0.0.1:9366
```

Wildcard site URLs look like:

```text
https://<site>.quick.example.com
```

## Guided setup

1. Install `quickd` so it listens on localhost, for example `127.0.0.1:9366`.
2. Create a Cloudflare Tunnel for the host.
3. Route both the wildcard and apex hostnames to the tunnel:

   ```yaml
   ingress:
     - hostname: "*.quick.example.com"
       service: http://127.0.0.1:9366
     - hostname: quick.example.com
       service: http://127.0.0.1:9366
     - service: http_status:404
   ```

4. In Cloudflare Zero Trust, create an Access application that covers both:
   - `quick.example.com`
   - `*.quick.example.com`
5. Add Access policies for the intended viewers, such as an email domain, IdP group, or named users.
6. Record the Access application AUD tag.
7. Configure `quickd` with the Cloudflare adapter:

   ```json
   {
     "iap": {
       "type": "cloudflare",
       "team_domain": "https://example.cloudflareaccess.com",
       "audience": "<Application AUD tag>",
       "jwks_url": "https://example.cloudflareaccess.com/cdn-cgi/access/certs",
       "email_domain_allowlist": ["example.com"]
     }
   }
   ```

8. Run `quick doctor --profile <name> --remote` and verify identity at `/_quick/identity`.

## JWT validation requirement

`quickd` must validate `Cf-Access-Jwt-Assertion` on every request. Do not trust `Cf-Access-Authenticated-User-Email` by itself.

Validation must check:

- signature and supported algorithm;
- `kid` against the cached JWKS, with refresh on unknown keys;
- issuer from the team domain;
- audience equal to the Access application AUD tag;
- expiry and not-before times;
- configured email domain or group restrictions.

The origin should not be reachable except through the tunnel. If the origin is exposed directly, scanners can bypass Access unless `quickd` rejects requests without a valid Access JWT.

## Operator notes

- Cloudflare manages edge TLS for proxied hostnames.
- `cloudflared` can proxy directly to quickd; Caddy is optional unless you want a local TLS hop.
- The Access application must cover the wildcard site hostnames, not only the apex control hostname.
- Keep the catch-all `http_status:404` ingress rule so unknown tunnel hostnames do not hit quickd accidentally.
