package identity

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"strings"
	"time"
)

type WhoIs struct {
	UserProfile struct {
		LoginName     string `json:"LoginName"`
		DisplayName   string `json:"DisplayName"`
		ProfilePicURL string `json:"ProfilePicURL"`
	} `json:"UserProfile"`
	Node struct {
		Name string `json:"Name"`
	} `json:"Node"`
	CapMap map[string][]string `json:"CapMap"`
}

type WhoIsClient interface {
	WhoIs(ctx context.Context, addr string) (*WhoIs, error)
}

type LocalAPIWhoIsClient struct {
	SocketPath string
	HTTPClient *http.Client
}

func (c *LocalAPIWhoIsClient) WhoIs(ctx context.Context, addr string) (*WhoIs, error) {
	socket := c.SocketPath
	if socket == "" {
		socket = "/var/run/tailscale/tailscaled.sock"
	}
	client := c.HTTPClient
	if client == nil {
		tr := &http.Transport{
			DialContext: func(ctx context.Context, network, address string) (net.Conn, error) {
				var d net.Dialer
				return d.DialContext(ctx, "unix", socket)
			},
		}
		client = &http.Client{Transport: tr, Timeout: 5 * time.Second}
	}
	u := "http://local-tailscaled.sock/localapi/v0/whois?addr=" + url.QueryEscape(addr)
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
	if err != nil {
		return nil, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("tailscale localapi whois status %d", resp.StatusCode)
	}
	var out WhoIs
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return &out, nil
}

type TailscaleLocalAPIAdapter struct {
	TrustedProxies []netip.Prefix
	SourceIPHeader string
	Client         WhoIsClient
}

func (a TailscaleLocalAPIAdapter) Name() string { return "tailscale-localapi" }

func (a TailscaleLocalAPIAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if a.Client == nil {
		return nil, fmt.Errorf("%w: missing tailscale whois client", ErrMisconfiguredAdapter)
	}
	peer, err := remoteAddrPort(r.RemoteAddr)
	if err != nil || !a.trusts(peer.Addr()) {
		return nil, ErrInvalidCredential
	}
	header := a.SourceIPHeader
	if header == "" {
		header = "X-Forwarded-For"
	}
	forwarded := strings.TrimSpace(r.Header.Get(header))
	if forwarded == "" {
		return nil, ErrMissingCredential
	}
	clientIP := strings.TrimSpace(strings.Split(forwarded, ",")[0])
	addr, err := netip.ParseAddr(clientIP)
	if err != nil {
		return nil, ErrInvalidCredential
	}
	lookup := netip.AddrPortFrom(addr, 0).String()
	if port := forwardedPort(r); port != "" {
		lookup = net.JoinHostPort(addr.String(), port)
	}
	who, err := a.Client.WhoIs(ctx, lookup)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrProviderUnavailable, err)
	}
	login := strings.TrimSpace(who.UserProfile.LoginName)
	if login == "" {
		return nil, ErrMissingCredential
	}
	id := &Identity{
		Authenticated: true,
		Provider:      "tailscale",
		Subject:       "tailscale:" + login,
		Email:         login,
		Login:         login,
		Name:          who.UserProfile.DisplayName,
		AvatarURL:     who.UserProfile.ProfilePicURL,
		Device:        strings.TrimSuffix(who.Node.Name, "."),
		Raw: map[string]string{
			"source_ip": addr.String(),
		},
	}
	if len(who.CapMap) > 0 {
		id.Capabilities = map[string]any{"tailscale_capabilities": who.CapMap}
	}
	return id, nil
}

func (a TailscaleLocalAPIAdapter) trusts(addr netip.Addr) bool {
	if len(a.TrustedProxies) == 0 {
		return addr.IsLoopback()
	}
	for _, p := range a.TrustedProxies {
		if p.Contains(addr) {
			return true
		}
	}
	return false
}

func remoteAddrPort(remote string) (netip.AddrPort, error) {
	host, port, err := net.SplitHostPort(remote)
	if err != nil {
		addr, err2 := netip.ParseAddr(strings.Trim(remote, "[]"))
		if err2 != nil {
			return netip.AddrPort{}, err
		}
		return netip.AddrPortFrom(addr, 0), nil
	}
	p := uint16(0)
	if port != "" {
		var n uint64
		fmt.Sscanf(port, "%d", &n)
		p = uint16(n)
	}
	addr, err := netip.ParseAddr(strings.Trim(host, "[]"))
	if err != nil {
		return netip.AddrPort{}, err
	}
	return netip.AddrPortFrom(addr, p), nil
}

func forwardedPort(r *http.Request) string {
	if xf := r.Header.Get("X-Forwarded-Port"); xf != "" {
		return strings.TrimSpace(strings.Split(xf, ",")[0])
	}
	_, port, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil {
		return port
	}
	return ""
}

func ParseTrustedProxies(in []string) ([]netip.Prefix, error) {
	out := make([]netip.Prefix, 0, len(in))
	for _, s := range in {
		s = strings.TrimSpace(s)
		if s == "" {
			continue
		}
		p, err := netip.ParsePrefix(s)
		if err != nil {
			addr, err2 := netip.ParseAddr(s)
			if err2 != nil {
				return nil, err
			}
			bits := 128
			if addr.Is4() {
				bits = 32
			}
			p = netip.PrefixFrom(addr, bits)
		}
		out = append(out, p)
	}
	return out, nil
}
