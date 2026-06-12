package deploy

import (
	"archive/zip"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type zipEntry struct {
	name      string
	body      string
	mode      os.FileMode
	encrypted bool
}

func makeZip(t *testing.T, entries []zipEntry) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "site.zip")
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	zw := zip.NewWriter(f)
	for _, e := range entries {
		h := &zip.FileHeader{Name: e.name, Method: zip.Deflate}
		if e.mode != 0 {
			h.SetMode(e.mode)
		}
		if e.encrypted {
			h.Flags |= 0x1
		}
		w, err := zw.CreateHeader(h)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := w.Write([]byte(e.body)); err != nil {
			t.Fatal(err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestExtractZipValidationAndSingleRoot(t *testing.T) {
	tests := []struct {
		name    string
		entries []zipEntry
		caps    ZipCaps
		wantErr string
		want    string
	}{
		{"single root stripped", []zipEntry{{"dist/index.html", "ok", 0, false}}, DefaultZipCaps(), "", "index.html"},
		{"traversal", []zipEntry{{"../x", "bad", 0, false}}, DefaultZipCaps(), "invalid path", ""},
		{"absolute", []zipEntry{{"/x", "bad", 0, false}}, DefaultZipCaps(), "absolute", ""},
		{"duplicate", []zipEntry{{"index.html", "one", 0, false}, {"index.html", "two", 0, false}}, DefaultZipCaps(), "duplicate", ""},
		{"symlink", []zipEntry{{"link", "../secret", os.ModeSymlink | 0o777, false}}, DefaultZipCaps(), "symlink", ""},
		{"encrypted", []zipEntry{{"index.html", "ok", 0, true}}, DefaultZipCaps(), "encrypted", ""},
		{"entry cap", []zipEntry{{"a", "a", 0, false}, {"b", "b", 0, false}}, ZipCaps{MaxEntries: 1}, "entries", ""},
		{"uncompressed cap", []zipEntry{{"big", strings.Repeat("x", 20), 0, false}}, ZipCaps{MaxUncompressed: 4, MaxCompressed: 1 << 20}, "uncompressed", ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			zipPath := makeZip(t, tt.entries)
			dest := t.TempDir()
			err := ExtractZip(zipPath, dest, tt.caps)
			if tt.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tt.wantErr) {
					t.Fatalf("err=%v want %q", err, tt.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			if _, err := os.Stat(filepath.Join(dest, tt.want)); err != nil {
				t.Fatalf("missing extracted %s: %v", tt.want, err)
			}
		})
	}
}
