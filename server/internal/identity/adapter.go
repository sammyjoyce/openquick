package identity

import (
	"context"
	"errors"
	"net"
	"net/http"
	"strings"
)

type Identity struct {
	Authenticated bool              `json:"authenticated"`
	Provider      string            `json:"provider"`
	Subject       string            `json:"subject"`
	Email         string            `json:"email,omitempty"`
	Login         string            `json:"login,omitempty"`
	Name          string            `json:"name,omitempty"`
	AvatarURL     string            `json:"avatar_url,omitempty"`
	Groups        []string          `json:"groups,omitempty"`
	Device        string            `json:"device,omitempty"`
	Capabilities  map[string]any    `json:"capabilities,omitempty"`
	Raw           map[string]string `json:"-"`
}

type Adapter interface {
	Name() string
	Authenticate(ctx context.Context, r *http.Request) (*Identity, error)
}

var (
	ErrMissingCredential    = errors.New("missing credential")
	ErrInvalidCredential    = errors.New("invalid credential")
	ErrProviderUnavailable  = errors.New("provider unavailable")
	ErrAnonymousNotAllowed  = errors.New("anonymous not allowed")
	ErrMisconfiguredAdapter = errors.New("misconfigured adapter")
)

func StatusForError(err error) int {
	switch {
	case err == nil:
		return http.StatusOK
	case errors.Is(err, ErrMissingCredential), errors.Is(err, ErrAnonymousNotAllowed):
		return http.StatusUnauthorized
	case errors.Is(err, ErrInvalidCredential):
		return http.StatusForbidden
	case errors.Is(err, ErrProviderUnavailable):
		return http.StatusServiceUnavailable
	case errors.Is(err, ErrMisconfiguredAdapter):
		return http.StatusInternalServerError
	default:
		return http.StatusInternalServerError
	}
}

type contextKey struct{}

func WithIdentity(ctx context.Context, id *Identity) context.Context {
	return context.WithValue(ctx, contextKey{}, id)
}

func FromContext(ctx context.Context) (*Identity, bool) {
	id, ok := ctx.Value(contextKey{}).(*Identity)
	return id, ok
}

func Anonymous() *Identity {
	return &Identity{Authenticated: false, Provider: "anonymous", Subject: "anonymous"}
}

func SubjectKey(id *Identity) string {
	if id == nil || id.Subject == "" {
		return "anonymous"
	}
	return id.Subject
}

func IsLoopbackRemote(remoteAddr string) bool {
	host, _, err := net.SplitHostPort(remoteAddr)
	if err != nil {
		host = remoteAddr
	}
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(strings.Trim(host, "[]"))
	return ip != nil && ip.IsLoopback()
}

func StripQuickHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		for name := range r.Header {
			if strings.HasPrefix(strings.ToLower(name), "x-quick-") {
				delete(r.Header, name)
			}
		}
		next.ServeHTTP(w, r)
	})
}

func DisplayNameFromEmail(email string) string {
	local := email
	if at := strings.IndexByte(local, '@'); at >= 0 {
		local = local[:at]
	}
	local = strings.ReplaceAll(local, ".", " ")
	local = strings.ReplaceAll(local, "_", " ")
	if local == "" {
		return email
	}
	return strings.Title(local)
}
