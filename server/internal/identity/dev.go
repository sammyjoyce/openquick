package identity

import (
	"context"
	"net/http"
	"strings"
)

type DevAdapter struct {
	Email             string
	AllowPublicUnsafe bool
}

func (a DevAdapter) Name() string { return "dev" }

func (a DevAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if !a.AllowPublicUnsafe && !IsLoopbackRemote(r.RemoteAddr) {
		return nil, ErrAnonymousNotAllowed
	}
	if strings.TrimSpace(a.Email) == "" {
		return Anonymous(), nil
	}
	email := strings.TrimSpace(a.Email)
	return &Identity{
		Authenticated: true,
		Provider:      "dev",
		Subject:       "dev:" + email,
		Email:         email,
		Login:         email,
		Name:          DisplayNameFromEmail(email),
	}, nil
}

type NoneAdapter struct {
	AllowPublicUnsafe bool
}

func (a NoneAdapter) Name() string { return "none" }

func (a NoneAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if !a.AllowPublicUnsafe && !IsLoopbackRemote(r.RemoteAddr) {
		return nil, ErrAnonymousNotAllowed
	}
	return Anonymous(), nil
}
