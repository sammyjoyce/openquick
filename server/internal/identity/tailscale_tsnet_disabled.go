//go:build !tsnet

package identity

import "fmt"

func newTailscaleTSNetAdapter() (Adapter, error) {
	return nil, fmt.Errorf("%w: tailscale-tsnet support is not compiled in", ErrMisconfiguredAdapter)
}
