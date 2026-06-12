package identity

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
)

type TailscaleServeAdapter struct{}

func (a TailscaleServeAdapter) Name() string { return "tailscale-serve" }

func (a TailscaleServeAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if !IsLoopbackRemote(r.RemoteAddr) {
		return nil, ErrInvalidCredential
	}
	login := strings.TrimSpace(r.Header.Get("Tailscale-User-Login"))
	if login == "" {
		return nil, ErrMissingCredential
	}
	name := strings.TrimSpace(r.Header.Get("Tailscale-User-Name"))
	avatar := strings.TrimSpace(r.Header.Get("Tailscale-User-Profile-Pic"))
	id := &Identity{
		Authenticated: true,
		Provider:      "tailscale",
		Subject:       "tailscale:" + login,
		Email:         login,
		Login:         login,
		Name:          name,
		AvatarURL:     avatar,
		Raw: map[string]string{
			"Tailscale-User-Login":       login,
			"Tailscale-User-Name":        name,
			"Tailscale-User-Profile-Pic": avatar,
		},
	}
	if caps := strings.TrimSpace(r.Header.Get("Tailscale-App-Capabilities")); caps != "" {
		var m map[string]any
		if json.Unmarshal([]byte(caps), &m) == nil {
			id.Capabilities = m
		}
		id.Raw["Tailscale-App-Capabilities"] = caps
	}
	return id, nil
}
