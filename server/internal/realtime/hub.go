package realtime

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"sync"
	"time"

	"nhooyr.io/websocket"

	"openquick.dev/quickd/internal/ratelimit"
)

type Hub struct {
	mu             sync.Mutex
	clients        map[*client]bool
	siteCounts     map[string]int
	MaxPerSite     int
	MessageLimit   int
	MessageWindow  time.Duration
	MessageLimiter *ratelimit.Limiter
}

type client struct {
	site     string
	identity string
	send     chan []byte
	channels map[string]bool
}

type Envelope struct {
	Type    string          `json:"type"`
	Channel string          `json:"channel,omitempty"`
	Since   string          `json:"since,omitempty"`
	Event   string          `json:"event,omitempty"`
	Data    json.RawMessage `json:"data,omitempty"`
}

func New() *Hub {
	return &Hub{clients: map[*client]bool{}, siteCounts: map[string]int{}, MaxPerSite: 100, MessageLimit: 120, MessageWindow: time.Minute, MessageLimiter: ratelimit.New()}
}

func (h *Hub) Serve(w http.ResponseWriter, r *http.Request, site, identityKey string) error {
	if h == nil {
		return errors.New("nil realtime hub")
	}
	if !h.reserve(site) {
		http.Error(w, "too many realtime connections", http.StatusTooManyRequests)
		return nil
	}
	defer h.release(site)
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		return err
	}
	defer conn.Close(websocket.StatusNormalClosure, "bye")
	c := &client{site: site, identity: identityKey, send: make(chan []byte, 32), channels: map[string]bool{}}
	h.add(c)
	defer h.remove(c)
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			select {
			case <-ctx.Done():
				return
			case msg, ok := <-c.send:
				if !ok {
					return
				}
				if err := conn.Write(ctx, websocket.MessageText, msg); err != nil {
					return
				}
			}
		}
	}()
	for {
		_, payload, err := conn.Read(ctx)
		if err != nil {
			cancel()
			<-done
			return nil
		}
		if !h.MessageLimiter.Allow("realtime", site+"/"+identityKey, h.MessageLimit, h.MessageWindow) {
			_ = writeError(ctx, conn, "rate_limited")
			continue
		}
		var env Envelope
		if err := json.Unmarshal(payload, &env); err != nil {
			_ = writeError(ctx, conn, "invalid_json")
			continue
		}
		switch env.Type {
		case "subscribe":
			if env.Channel == "" {
				_ = writeError(ctx, conn, "missing_channel")
				continue
			}
			h.subscribe(c, env.Channel)
			_ = writeAck(ctx, conn, "subscribed", env.Channel)
		case "unsubscribe":
			h.unsubscribe(c, env.Channel)
			_ = writeAck(ctx, conn, "unsubscribed", env.Channel)
		case "publish":
			if env.Channel == "" {
				_ = writeError(ctx, conn, "missing_channel")
				continue
			}
			h.PublishRaw(site, env.Channel, env.Event, env.Data)
		default:
			_ = writeError(ctx, conn, "unknown_type")
		}
	}
}

func (h *Hub) reserve(site string) bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.clients == nil {
		h.clients = map[*client]bool{}
	}
	if h.siteCounts == nil {
		h.siteCounts = map[string]int{}
	}
	limit := h.MaxPerSite
	if limit <= 0 {
		limit = 100
	}
	if h.siteCounts[site] >= limit {
		return false
	}
	h.siteCounts[site]++
	return true
}

func (h *Hub) release(site string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.siteCounts[site] > 0 {
		h.siteCounts[site]--
	}
}

func (h *Hub) add(c *client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.clients[c] = true
}

func (h *Hub) remove(c *client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.clients, c)
	close(c.send)
}

func (h *Hub) subscribe(c *client, channel string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	c.channels[channel] = true
}

func (h *Hub) unsubscribe(c *client, channel string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(c.channels, channel)
}

func (h *Hub) Publish(site, channel, event string, data any) {
	b, _ := json.Marshal(data)
	h.PublishRaw(site, channel, event, b)
}

func (h *Hub) PublishRaw(site, channel, event string, data json.RawMessage) {
	if h == nil {
		return
	}
	out, err := json.Marshal(Envelope{Type: "event", Channel: channel, Event: event, Data: data})
	if err != nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	for c := range h.clients {
		if c.site != site || !c.channels[channel] {
			continue
		}
		select {
		case c.send <- out:
		default:
		}
	}
}

func writeError(ctx context.Context, c *websocket.Conn, code string) error {
	b, _ := json.Marshal(map[string]string{"type": "error", "error": code})
	return c.Write(ctx, websocket.MessageText, b)
}

func writeAck(ctx context.Context, c *websocket.Conn, typ, channel string) error {
	b, _ := json.Marshal(map[string]string{"type": typ, "channel": channel})
	return c.Write(ctx, websocket.MessageText, b)
}
