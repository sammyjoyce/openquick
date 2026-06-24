//go:build tsnet

package identity

import "tailscale.com/tsnet"

func newTailscaleTSNetAdapter() (Adapter, error) {
	return TailscaleTSNetAdapter{Server: &tsnet.Server{}}, nil
}
