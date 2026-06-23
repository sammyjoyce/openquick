package scan

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"unicode/utf8"
)

type Limits struct {
	MaxFiles        int
	MaxBytesPerFile int64
}

type Finding struct {
	File    string `json:"file"`
	Pattern string `json:"pattern"`
}

type Report struct {
	Static   bool      `json:"static"`
	Findings []Finding `json:"findings"`
}

var patterns = []string{
	"quick.db",
	"quick.uploads",
	"quick.ai",
	"quick.identity",
	"quick.realtime",
	"/_quick/",
}

func DefaultLimits() Limits {
	return Limits{MaxFiles: 2000, MaxBytesPerFile: 1 << 20}
}

func (l Limits) withDefaults() Limits {
	if l.MaxFiles <= 0 {
		l.MaxFiles = 2000
	}
	if l.MaxBytesPerFile <= 0 {
		l.MaxBytesPerFile = 1 << 20
	}
	return l
}

func Scan(dir string, limits Limits) (Report, error) {
	limits = limits.withDefaults()
	report := Report{Static: true}
	filesSeen := 0
	err := filepath.WalkDir(dir, func(p string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if p == dir {
			return nil
		}
		if d.Type()&os.ModeSymlink != 0 {
			return nil
		}
		if d.IsDir() {
			return nil
		}
		filesSeen++
		if filesSeen > limits.MaxFiles {
			return filepath.SkipAll
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		if info.Size() == 0 {
			return nil
		}
		f, err := os.Open(p)
		if err != nil {
			return err
		}
		defer f.Close()
		buf, err := io.ReadAll(io.LimitReader(f, limits.MaxBytesPerFile))
		if err != nil {
			return err
		}
		if binary(buf) {
			return nil
		}
		text := string(buf)
		for _, pat := range patterns {
			if strings.Contains(text, pat) {
				rel, err := filepath.Rel(dir, p)
				if err != nil {
					rel = p
				}
				report.Findings = append(report.Findings, Finding{File: filepath.ToSlash(rel), Pattern: pat})
				report.Static = false
			}
		}
		return nil
	})
	if err != nil {
		return Report{}, err
	}
	return report, nil
}

func FormatFindings(findings []Finding, limit int) string {
	if len(findings) == 0 {
		return "no findings"
	}
	if limit <= 0 || limit > len(findings) {
		limit = len(findings)
	}
	parts := make([]string, 0, limit+1)
	for i := 0; i < limit; i++ {
		parts = append(parts, fmt.Sprintf("%s matched %q", findings[i].File, findings[i].Pattern))
	}
	if len(findings) > limit {
		parts = append(parts, fmt.Sprintf("and %d more", len(findings)-limit))
	}
	return strings.Join(parts, "; ")
}

func binary(b []byte) bool {
	if len(b) == 0 {
		return false
	}
	probe := b
	if len(probe) > 8000 {
		probe = probe[:8000]
	}
	if bytes.IndexByte(probe, 0) >= 0 {
		return true
	}
	return !utf8.Valid(probe)
}
