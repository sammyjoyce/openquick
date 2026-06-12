package uploads

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"openquick.dev/quickd/internal/store"
)

var ErrTooLarge = errors.New("upload too large")

type Manager struct {
	Root           string
	MaxUploadBytes int64
	Store          *store.Store
}

func New(root string, maxBytes int64, st *store.Store) *Manager {
	if maxBytes <= 0 {
		maxBytes = 100 << 20
	}
	return &Manager{Root: root, MaxUploadBytes: maxBytes, Store: st}
}

func (m *Manager) SaveRaw(ctx context.Context, site, actor, name, contentType string, body io.Reader) (store.Upload, error) {
	if m.Store == nil {
		return store.Upload{}, errors.New("uploads: nil store")
	}
	data, err := readLimited(body, m.MaxUploadBytes)
	if err != nil {
		return store.Upload{}, err
	}
	if contentType == "" {
		contentType = http.DetectContentType(data)
	}
	id, err := objectID()
	if err != nil {
		return store.Upload{}, err
	}
	now := time.Now().UTC()
	dir := filepath.Join(m.Root, "uploads", site, now.Format("2006"), now.Format("01"))
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return store.Upload{}, err
	}
	path := filepath.Join(dir, id)
	if err := os.WriteFile(path, data, 0o640); err != nil {
		return store.Upload{}, err
	}
	u, err := m.Store.CreateUpload(ctx, site, id, path, contentType, int64(len(data)), actor)
	if err != nil {
		_ = os.Remove(path)
		return store.Upload{}, err
	}
	u.Name = name
	return u, nil
}

func (m *Manager) SaveMultipart(ctx context.Context, site, actor string, mr *multipart.Reader) (store.Upload, error) {
	for {
		part, err := mr.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			return store.Upload{}, err
		}
		if part.FileName() == "" {
			continue
		}
		defer part.Close()
		name := sanitizeName(part.FileName())
		return m.SaveRaw(ctx, site, actor, name, part.Header.Get("Content-Type"), part)
	}
	return store.Upload{}, errors.New("multipart upload missing file part")
}

func (m *Manager) Serve(ctx context.Context, site, id string, w http.ResponseWriter, r *http.Request) error {
	u, err := m.Store.GetUpload(ctx, site, id)
	if err != nil {
		return err
	}
	f, err := os.Open(u.Path)
	if err != nil {
		return err
	}
	defer f.Close()
	created, _ := time.Parse(time.RFC3339Nano, u.CreatedAt)
	w.Header().Set("Content-Type", u.ContentType)
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeContent(w, r, id, created, f)
	return nil
}

func (m *Manager) Delete(ctx context.Context, site, id string) error {
	u, err := m.Store.DeleteUpload(ctx, site, id)
	if err != nil {
		return err
	}
	return os.Remove(u.Path)
}

func readLimited(r io.Reader, max int64) ([]byte, error) {
	var buf bytes.Buffer
	lr := &io.LimitedReader{R: r, N: max + 1}
	if _, err := io.Copy(&buf, lr); err != nil {
		return nil, err
	}
	if int64(buf.Len()) > max {
		return nil, ErrTooLarge
	}
	return buf.Bytes(), nil
}

func objectID() (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(b[:]), nil
}

func sanitizeName(name string) string {
	name = filepath.Base(name)
	name = strings.Map(func(r rune) rune {
		if r < 32 || r == '/' || r == '\\' {
			return -1
		}
		return r
	}, name)
	if name == "." || name == "" {
		return "upload"
	}
	return name
}

func ErrorStatus(err error) int {
	switch {
	case errors.Is(err, ErrTooLarge):
		return http.StatusRequestEntityTooLarge
	case errors.Is(err, store.ErrNotFound):
		return http.StatusNotFound
	default:
		return http.StatusBadRequest
	}
}

func URL(id string) string { return fmt.Sprintf("/_quick/uploads/%s", id) }
