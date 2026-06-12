package scan

import (
	"os"
	"path/filepath"
	"testing"
)

func TestScanStaticFindingsBinaryAndCaps(t *testing.T) {
	tests := []struct {
		name       string
		files      map[string][]byte
		limits     Limits
		wantStatic bool
		want       int
	}{
		{"clean", map[string][]byte{"index.html": []byte("<h1>ok</h1>")}, Limits{}, true, 0},
		{"sdk usage", map[string][]byte{"app.js": []byte("quick.db.collection('x'); fetch('/_quick/identity')")}, Limits{}, false, 2},
		{"binary skipped", map[string][]byte{"blob.bin": []byte{'q', 'u', 'i', 'c', 'k', '.', 'd', 'b', 0}}, Limits{}, true, 0},
		{"file cap", map[string][]byte{"a.js": []byte("quick.uploads"), "b.js": []byte("quick.ai")}, Limits{MaxFiles: 1}, false, 1},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			dir := t.TempDir()
			for name, body := range tt.files {
				p := filepath.Join(dir, filepath.FromSlash(name))
				if err := os.MkdirAll(filepath.Dir(p), 0o770); err != nil {
					t.Fatal(err)
				}
				if err := os.WriteFile(p, body, 0o660); err != nil {
					t.Fatal(err)
				}
			}
			report, err := Scan(dir, tt.limits)
			if err != nil {
				t.Fatal(err)
			}
			if report.Static != tt.wantStatic || len(report.Findings) != tt.want {
				t.Fatalf("report=%+v want static=%v findings=%d", report, tt.wantStatic, tt.want)
			}
		})
	}
}
