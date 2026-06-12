package deploy

import (
	"archive/zip"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
)

type ZipCaps struct {
	MaxEntries      int
	MaxUncompressed int64
	MaxCompressed   int64
	StripSingleRoot bool
}

func DefaultZipCaps() ZipCaps {
	return ZipCaps{MaxEntries: 1000, MaxUncompressed: 100 << 20, MaxCompressed: 50 << 20, StripSingleRoot: true}
}

func (c ZipCaps) withDefaults() ZipCaps {
	if c.MaxEntries <= 0 {
		c.MaxEntries = 1000
	}
	if c.MaxUncompressed <= 0 {
		c.MaxUncompressed = 100 << 20
	}
	if c.MaxCompressed <= 0 {
		c.MaxCompressed = 50 << 20
	}
	return c
}

func ExtractZip(zipPath, destDir string, caps ZipCaps) error {
	caps = caps.withDefaults()
	zr, err := zip.OpenReader(zipPath)
	if err != nil {
		return err
	}
	defer zr.Close()
	if len(zr.File) > caps.MaxEntries {
		return fmt.Errorf("zip has %d entries, max %d", len(zr.File), caps.MaxEntries)
	}
	stripRoot := ""
	if caps.StripSingleRoot {
		stripRoot = singleRoot(zr.File)
	}
	destAbs, err := filepath.Abs(destDir)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(destAbs, 0o770); err != nil {
		return err
	}
	seen := map[string]bool{}
	var totalCompressed int64
	var totalUncompressed int64
	for _, f := range zr.File {
		if f.Flags&0x1 != 0 {
			return fmt.Errorf("zip entry %s is encrypted", f.Name)
		}
		if f.FileInfo().Mode()&os.ModeSymlink != 0 {
			return fmt.Errorf("zip entry %s is a symlink", f.Name)
		}
		totalCompressed += int64(f.CompressedSize64)
		if totalCompressed > caps.MaxCompressed {
			return fmt.Errorf("zip compressed size exceeds %d bytes", caps.MaxCompressed)
		}
		totalUncompressed += int64(f.UncompressedSize64)
		if totalUncompressed > caps.MaxUncompressed {
			return fmt.Errorf("zip uncompressed size exceeds %d bytes", caps.MaxUncompressed)
		}
		rel, skip, err := normalizedZipName(f.Name, stripRoot)
		if err != nil {
			return err
		}
		if skip {
			continue
		}
		if seen[rel] {
			return fmt.Errorf("zip duplicate entry %s", rel)
		}
		seen[rel] = true
		out := filepath.Join(destAbs, filepath.FromSlash(rel))
		if !pathWithin(destAbs, out) {
			return fmt.Errorf("zip entry %s escapes destination", f.Name)
		}
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(out, 0o770); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(out), 0o770); err != nil {
			return err
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		if err := writeZipFile(out, rc, int64(f.UncompressedSize64), caps.MaxUncompressed); err != nil {
			rc.Close()
			return err
		}
		if err := rc.Close(); err != nil {
			return err
		}
		mode := f.FileInfo().Mode().Perm()
		if mode == 0 {
			mode = 0o660
		}
		if err := os.Chmod(out, mode&0o777); err != nil {
			return err
		}
	}
	return nil
}

func writeZipFile(path string, rc io.Reader, declared, archiveLimit int64) error {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o660)
	if err != nil {
		return err
	}
	defer f.Close()
	limit := declared
	if limit <= 0 || limit > archiveLimit {
		limit = archiveLimit
	}
	n, err := io.Copy(f, io.LimitReader(rc, limit+1))
	if err != nil {
		return err
	}
	if n > limit {
		return errors.New("zip entry exceeds declared or configured size")
	}
	return nil
}

func singleRoot(files []*zip.File) string {
	root := ""
	for _, f := range files {
		name := strings.Trim(f.Name, "/")
		if name == "" {
			continue
		}
		parts := strings.SplitN(name, "/", 2)
		if len(parts) < 2 {
			return ""
		}
		if root == "" {
			root = parts[0]
		} else if root != parts[0] {
			return ""
		}
	}
	return root
}

func normalizedZipName(name, stripRoot string) (rel string, skip bool, err error) {
	if name == "" || strings.HasPrefix(name, "/") || strings.HasPrefix(name, `\\`) || filepath.IsAbs(name) || looksLikeWindowsAbs(name) {
		return "", false, fmt.Errorf("zip entry %q has absolute path", name)
	}
	name = strings.ReplaceAll(name, `\\`, "/")
	for _, seg := range strings.Split(name, "/") {
		if seg == ".." {
			return "", false, fmt.Errorf("zip entry %q contains invalid path segment", name)
		}
	}
	clean := path.Clean("/" + name)
	clean = strings.TrimPrefix(clean, "/")
	if clean == "." || clean == "" {
		return "", true, nil
	}
	for _, seg := range strings.Split(clean, "/") {
		if seg == ".." || seg == "" {
			return "", false, fmt.Errorf("zip entry %q contains invalid path segment", name)
		}
	}
	if stripRoot != "" {
		if clean == stripRoot {
			return "", true, nil
		}
		prefix := stripRoot + "/"
		if strings.HasPrefix(clean, prefix) {
			clean = strings.TrimPrefix(clean, prefix)
		}
	}
	if clean == "" || clean == "." {
		return "", true, nil
	}
	return clean, false, nil
}

func looksLikeWindowsAbs(name string) bool {
	return len(name) >= 3 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':' && (name[2] == '/' || name[2] == '\\')
}

func pathWithin(root, candidate string) bool {
	rootAbs, err := filepath.Abs(root)
	if err != nil {
		return false
	}
	candAbs, err := filepath.Abs(candidate)
	if err != nil {
		return false
	}
	rel, err := filepath.Rel(rootAbs, candAbs)
	if err != nil {
		return false
	}
	return rel == "." || (rel != "" && !strings.HasPrefix(rel, "..") && !filepath.IsAbs(rel))
}
