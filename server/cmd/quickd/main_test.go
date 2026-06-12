package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"openquick.dev/quickd/internal/store"
)

func TestAdminMintDevTokenStoresHashAndExpiry(t *testing.T) {
	root := t.TempDir()
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := st.EnsureSite(context.Background(), "demo", "demo"); err != nil {
		t.Fatal(err)
	}
	st.Close()

	out := captureStdout(t, func() {
		if err := adminCmd([]string{"mint-dev-token", "--site", "demo", "--ttl", "3600", "--json", "--root", root}); err != nil {
			t.Fatalf("admin mint-dev-token: %v", err)
		}
	})
	var res struct {
		Token     string `json:"token"`
		Site      string `json:"site"`
		ExpiresAt string `json:"expires_at"`
	}
	if err := json.Unmarshal([]byte(out), &res); err != nil {
		t.Fatalf("decode %q: %v", out, err)
	}
	if res.Token == "" || res.Site != "demo" {
		t.Fatalf("unexpected response: %+v", res)
	}
	expires, err := time.Parse("2006-01-02T15:04:05.000000000Z07:00", res.ExpiresAt)
	if err != nil || time.Until(expires) <= 0 {
		t.Fatalf("expires_at=%q err=%v", res.ExpiresAt, err)
	}

	st, err = store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	var tokenHash, subject string
	if err := st.DB.QueryRow(`SELECT token_hash, subject FROM dev_tokens WHERE site='demo'`).Scan(&tokenHash, &subject); err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256([]byte(res.Token))
	if tokenHash != hex.EncodeToString(sum[:]) || strings.Contains(tokenHash, res.Token) || subject == "" {
		t.Fatalf("token storage hash=%q subject=%q", tokenHash, subject)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "wrong", res.Token, time.Now()); err != nil || ok {
		t.Fatalf("wrong-site validation ok=%v err=%v", ok, err)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "demo", res.Token, time.Now()); err != nil || !ok {
		t.Fatalf("token validation ok=%v err=%v", ok, err)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "demo", res.Token, time.Now().Add(2*time.Hour)); err != nil || ok {
		t.Fatalf("expired validation ok=%v err=%v", ok, err)
	}
	var remaining int
	if err := st.DB.QueryRow(`SELECT COUNT(*) FROM dev_tokens WHERE site='demo'`).Scan(&remaining); err != nil || remaining != 0 {
		t.Fatalf("expired prune remaining=%d err=%v", remaining, err)
	}
}

func TestServeRemoteAPIValidation(t *testing.T) {
	if err := serveCmd([]string{"--remote-api", "https://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "--dev") {
		t.Fatalf("remote without dev err=%v", err)
	}
	if err := serveCmd([]string{"--dev", "--listen", "0.0.0.0:9366", "--remote-api", "https://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "loopback") {
		t.Fatalf("remote public listen err=%v", err)
	}
	if err := serveCmd([]string{"--dev", "--remote-api", "http://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "https") {
		t.Fatalf("remote http err=%v", err)
	}
}

func captureStdout(t *testing.T, fn func()) string {
	t.Helper()
	old := os.Stdout
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdout = w
	defer func() { os.Stdout = old }()
	fn()
	_ = w.Close()
	data, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}
