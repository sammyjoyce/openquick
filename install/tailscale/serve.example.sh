#!/usr/bin/env sh
set -eu

# Serve quickd from the local-only origin. In tailscale-serve mode quickd may
# trust Tailscale-User-* identity headers only from this localhost proxy path.
tailscale serve --bg https / http://127.0.0.1:9366

# Do not assume Tailscale Funnel is an identity boundary: public Funnel traffic
# does not carry tailnet user identity headers. Pair Funnel with another IAP or
# run it only for intentional public previews.
