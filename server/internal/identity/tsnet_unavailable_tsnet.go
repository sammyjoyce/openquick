//go:build tsnet

package identity

import "fmt"

func tsnetAdapterUnavailable(selector string) error {
	return fmt.Errorf("%w: %s requires quickd serve to create the shared tsnet.Server", ErrMisconfiguredAdapter, selector)
}
