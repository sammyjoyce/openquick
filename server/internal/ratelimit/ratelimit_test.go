package ratelimit

import (
	"testing"
	"time"
)

func TestFixedWindowLimiter(t *testing.T) {
	l := New()
	if !l.Allow("scope", "key", 2, time.Minute) || !l.Allow("scope", "key", 2, time.Minute) {
		t.Fatalf("first two should pass")
	}
	if l.Allow("scope", "key", 2, time.Minute) {
		t.Fatalf("third should be rate limited")
	}
	if !l.Allow("scope", "other", 2, time.Minute) {
		t.Fatalf("different key should pass")
	}
}
