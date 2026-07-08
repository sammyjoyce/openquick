package store

import (
	"context"
	"errors"
	"path/filepath"
	"testing"
)

func TestConditionalDocumentWrites(t *testing.T) {
	ctx := context.Background()
	st, err := Open(filepath.Join(t.TempDir(), "data"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = st.Close() })
	if _, err := st.EnsureSite(ctx, "a", "a"); err != nil {
		t.Fatal(err)
	}
	created, err := st.CreateDocument(ctx, "a", "posts", "one", `{"title":"one"}`, "alice")
	if err != nil {
		t.Fatal(err)
	}
	updated, err := st.PutDocumentIfUnchanged(ctx, "a", "posts", "one", `{"title":"two"}`, "bob", created.UpdatedAt, created.DataJSON)
	if err != nil {
		t.Fatalf("conditional put: %v", err)
	}
	if updated.DataJSON != `{"title":"two"}` || updated.UpdatedBy != "bob" {
		t.Fatalf("updated=%+v", updated)
	}
	if _, err := st.PutDocumentIfUnchanged(ctx, "a", "posts", "one", `{"title":"stale"}`, "carol", created.UpdatedAt, created.DataJSON); !errors.Is(err, ErrRevisionMismatch) {
		t.Fatalf("stale conditional put err=%v", err)
	}
	if err := st.DeleteDocumentIfUnchanged(ctx, "a", "posts", "one", created.UpdatedAt, created.DataJSON); !errors.Is(err, ErrRevisionMismatch) {
		t.Fatalf("stale conditional delete err=%v", err)
	}
	if err := st.DeleteDocumentIfUnchanged(ctx, "a", "posts", "missing", updated.UpdatedAt, updated.DataJSON); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing conditional delete err=%v", err)
	}
	if err := st.DeleteDocumentIfUnchanged(ctx, "a", "posts", "one", updated.UpdatedAt, updated.DataJSON); err != nil {
		t.Fatalf("conditional delete: %v", err)
	}
	if _, err := st.GetDocument(ctx, "a", "posts", "one"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("get deleted err=%v", err)
	}
}
