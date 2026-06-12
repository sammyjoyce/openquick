//go:build tsnet

package identity

import (
	"context"
	"net/http"
	"strings"

	"tailscale.com/tsnet"
)

// TailscaleTSNetAdapter is built only with -tags tsnet. The default quickd
// build intentionally excludes tailscale.com modules so normal host builds stay
// small; operators who want an embedded tailnet listener can opt into this file.
type TailscaleTSNetAdapter struct {
	Server *tsnet.Server
}

func (a TailscaleTSNetAdapter) Name() string { return "tailscale-tsnet" }

func (a TailscaleTSNetAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if a.Server == nil {
		return nil, ErrMisconfiguredAdapter
	}
	lc, err := a.Server.LocalClient()
	if err != nil {
		return nil, ErrProviderUnavailable
	}
	who, err := lc.WhoIs(ctx, r.RemoteAddr)
	if err != nil {
		return nil, ErrInvalidCredential
	}
	login := strings.TrimSpace(who.UserProfile.LoginName)
	if login == "" {
		return nil, ErrMissingCredential
	}
	return &Identity{
		Authenticated: true,
		Provider:      "tailscale",
		Subject:       "tailscale:" + login,
		Email:         login,
		Login:         login,
		Name:          who.UserProfile.DisplayName,
		AvatarURL:     who.UserProfile.ProfilePicURL,
		Device:        strings.TrimSuffix(who.Node.Name, "."),
	}, nil
}
