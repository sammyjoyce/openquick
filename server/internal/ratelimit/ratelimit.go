package ratelimit

import (
	"sync"
	"time"
)

type Limiter struct {
	mu       sync.Mutex
	counters map[string]entry
	now      func() time.Time
}

type entry struct {
	window int64
	count  int
}

func New() *Limiter {
	return &Limiter{counters: map[string]entry{}, now: time.Now}
}

func (l *Limiter) Allow(scope, key string, limit int, window time.Duration) bool {
	if limit <= 0 {
		return true
	}
	if window <= 0 {
		window = time.Minute
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.counters == nil {
		l.counters = map[string]entry{}
	}
	now := l.now().UnixNano()
	w := now / int64(window)
	k := scope + "\x00" + key
	e := l.counters[k]
	if e.window != w {
		e = entry{window: w}
	}
	if e.count >= limit {
		l.counters[k] = e
		return false
	}
	e.count++
	l.counters[k] = e
	return true
}
