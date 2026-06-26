//go:build !tsnet

package identity

import "fmt"

func tsnetAdapterUnavailable(selector string) error {
	return fmt.Errorf("%w: %s requires quickd built with -tags tsnet", ErrMisconfiguredAdapter, selector)
}
