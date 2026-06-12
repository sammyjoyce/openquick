package store

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

type Store struct {
	DB *sql.DB
}

type SiteRecord struct {
	ID        int64  `json:"-"`
	Name      string `json:"name"`
	Subdomain string `json:"-"`
	URL       string `json:"url"`
	Release   string `json:"release"`
	UpdatedAt string `json:"updated_at"`
	Deployer  string `json:"deployer"`
}

type Document struct {
	SiteID     int64
	Collection string
	ID         string `json:"id"`
	DataJSON   string `json:"-"`
	CreatedBy  string `json:"created_by,omitempty"`
	UpdatedBy  string `json:"updated_by,omitempty"`
	CreatedAt  string `json:"created_at"`
	UpdatedAt  string `json:"updated_at"`
}

type Upload struct {
	SiteID      int64
	ID          string `json:"id"`
	Path        string `json:"-"`
	ContentType string `json:"content_type"`
	Size        int64  `json:"size"`
	CreatedBy   string `json:"created_by,omitempty"`
	CreatedAt   string `json:"created_at"`
	Name        string `json:"name,omitempty"`
}

var ErrNotFound = errors.New("not found")

func Open(dataDir string) (*Store, error) {
	if err := os.MkdirAll(dataDir, 0o750); err != nil {
		return nil, err
	}
	path := filepath.Join(dataDir, "quick.db")
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	st := &Store{DB: db}
	if err := st.init(); err != nil {
		db.Close()
		return nil, err
	}
	return st, nil
}

func OpenMemory() (*Store, error) {
	db, err := sql.Open("sqlite", "file:quickd-test?mode=memory&cache=shared")
	if err != nil {
		return nil, err
	}
	st := &Store{DB: db}
	if err := st.init(); err != nil {
		db.Close()
		return nil, err
	}
	return st, nil
}

func (s *Store) Close() error {
	if s == nil || s.DB == nil {
		return nil
	}
	return s.DB.Close()
}

func (s *Store) init() error {
	pragmas := []string{
		"PRAGMA journal_mode=WAL;",
		"PRAGMA foreign_keys=ON;",
		"PRAGMA busy_timeout=5000;",
	}
	for _, p := range pragmas {
		if _, err := s.DB.Exec(p); err != nil {
			return err
		}
	}
	stmts := []string{
		`CREATE TABLE IF NOT EXISTS sites (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name TEXT NOT NULL UNIQUE,
			subdomain TEXT NOT NULL UNIQUE,
			created_at TEXT NOT NULL,
			updated_at TEXT NOT NULL,
			last_release_id TEXT
		);`,
		`CREATE TABLE IF NOT EXISTS deploys (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			site_id INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
			release_id TEXT NOT NULL,
			deployer TEXT,
			bytes INTEGER NOT NULL,
			files INTEGER NOT NULL,
			created_at TEXT NOT NULL
		);`,
		`CREATE TABLE IF NOT EXISTS documents (
			site_id INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
			collection TEXT NOT NULL,
			id TEXT NOT NULL,
			data_json TEXT NOT NULL,
			created_by TEXT,
			updated_by TEXT,
			created_at TEXT NOT NULL,
			updated_at TEXT NOT NULL,
			PRIMARY KEY(site_id, collection, id)
		);`,
		`CREATE TABLE IF NOT EXISTS uploads (
			site_id INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
			id TEXT NOT NULL,
			path TEXT NOT NULL,
			content_type TEXT NOT NULL,
			size INTEGER NOT NULL,
			created_by TEXT,
			created_at TEXT NOT NULL,
			PRIMARY KEY(site_id, id)
		);`,
		`CREATE TABLE IF NOT EXISTS rate_limits (
			scope TEXT NOT NULL,
			key TEXT NOT NULL,
			window_start INTEGER NOT NULL,
			count INTEGER NOT NULL,
			PRIMARY KEY(scope, key, window_start)
		);`,
	}
	for _, stmt := range stmts {
		if _, err := s.DB.Exec(stmt); err != nil {
			return err
		}
	}
	return nil
}

func now() string { return time.Now().UTC().Format(time.RFC3339Nano) }

func (s *Store) EnsureSite(ctx context.Context, name, subdomain string) (int64, error) {
	if subdomain == "" {
		subdomain = name
	}
	t := now()
	res, err := s.DB.ExecContext(ctx, `INSERT INTO sites(name, subdomain, created_at, updated_at) VALUES(?,?,?,?) ON CONFLICT(name) DO UPDATE SET subdomain=excluded.subdomain, updated_at=sites.updated_at`, name, subdomain, t, t)
	if err != nil {
		return 0, err
	}
	if id, err := res.LastInsertId(); err == nil && id != 0 {
		return id, nil
	}
	return s.SiteID(ctx, name)
}

func (s *Store) SiteID(ctx context.Context, name string) (int64, error) {
	var id int64
	if err := s.DB.QueryRowContext(ctx, `SELECT id FROM sites WHERE name = ?`, name).Scan(&id); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return 0, ErrNotFound
		}
		return 0, err
	}
	return id, nil
}

func (s *Store) GetSite(ctx context.Context, name string) (SiteRecord, error) {
	row := s.DB.QueryRowContext(ctx, `SELECT s.id, s.name, s.subdomain, COALESCE(s.last_release_id,''), s.updated_at, COALESCE((SELECT d.deployer FROM deploys d WHERE d.site_id=s.id ORDER BY d.created_at DESC, d.id DESC LIMIT 1),'') FROM sites s WHERE s.name = ?`, name)
	var rec SiteRecord
	if err := row.Scan(&rec.ID, &rec.Name, &rec.Subdomain, &rec.Release, &rec.UpdatedAt, &rec.Deployer); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return SiteRecord{}, ErrNotFound
		}
		return SiteRecord{}, err
	}
	return rec, nil
}

func (s *Store) ListSites(ctx context.Context) ([]SiteRecord, error) {
	rows, err := s.DB.QueryContext(ctx, `SELECT s.id, s.name, s.subdomain, COALESCE(s.last_release_id,''), s.updated_at, COALESCE((SELECT d.deployer FROM deploys d WHERE d.site_id=s.id ORDER BY d.created_at DESC, d.id DESC LIMIT 1),'') FROM sites s ORDER BY s.name`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []SiteRecord
	for rows.Next() {
		var rec SiteRecord
		if err := rows.Scan(&rec.ID, &rec.Name, &rec.Subdomain, &rec.Release, &rec.UpdatedAt, &rec.Deployer); err != nil {
			return nil, err
		}
		out = append(out, rec)
	}
	return out, rows.Err()
}

func (s *Store) DeleteSite(ctx context.Context, name string) error {
	res, err := s.DB.ExecContext(ctx, `DELETE FROM sites WHERE name = ?`, name)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) RecordDeploy(ctx context.Context, site, release, deployer string, bytes int64, files int) error {
	tx, err := s.DB.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	t := now()
	_, err = tx.ExecContext(ctx, `INSERT INTO sites(name, subdomain, created_at, updated_at, last_release_id) VALUES(?,?,?,?,?) ON CONFLICT(name) DO UPDATE SET last_release_id=excluded.last_release_id, updated_at=excluded.updated_at`, site, site, t, t, release)
	if err != nil {
		return err
	}
	var siteID int64
	if err := tx.QueryRowContext(ctx, `SELECT id FROM sites WHERE name = ?`, site).Scan(&siteID); err != nil {
		return err
	}
	_, err = tx.ExecContext(ctx, `INSERT INTO deploys(site_id, release_id, deployer, bytes, files, created_at) VALUES(?,?,?,?,?,?)`, siteID, release, deployer, bytes, files, t)
	if err != nil {
		return err
	}
	return tx.Commit()
}

func (s *Store) CreateDocument(ctx context.Context, site, collection, id, dataJSON, actor string) (Document, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return Document{}, err
	}
	t := now()
	_, err = s.DB.ExecContext(ctx, `INSERT INTO documents(site_id, collection, id, data_json, created_by, updated_by, created_at, updated_at) VALUES(?,?,?,?,?,?,?,?)`, siteID, collection, id, dataJSON, actor, actor, t, t)
	if err != nil {
		return Document{}, err
	}
	return Document{SiteID: siteID, Collection: collection, ID: id, DataJSON: dataJSON, CreatedBy: actor, UpdatedBy: actor, CreatedAt: t, UpdatedAt: t}, nil
}

func (s *Store) GetDocument(ctx context.Context, site, collection, id string) (Document, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return Document{}, err
	}
	row := s.DB.QueryRowContext(ctx, `SELECT data_json, COALESCE(created_by,''), COALESCE(updated_by,''), created_at, updated_at FROM documents WHERE site_id=? AND collection=? AND id=?`, siteID, collection, id)
	var d Document
	d.SiteID, d.Collection, d.ID = siteID, collection, id
	if err := row.Scan(&d.DataJSON, &d.CreatedBy, &d.UpdatedBy, &d.CreatedAt, &d.UpdatedAt); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return Document{}, ErrNotFound
		}
		return Document{}, err
	}
	return d, nil
}

func (s *Store) ListDocuments(ctx context.Context, site, collection string) ([]Document, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return nil, err
	}
	rows, err := s.DB.QueryContext(ctx, `SELECT id, data_json, COALESCE(created_by,''), COALESCE(updated_by,''), created_at, updated_at FROM documents WHERE site_id=? AND collection=? ORDER BY created_at, id`, siteID, collection)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Document
	for rows.Next() {
		var d Document
		d.SiteID, d.Collection = siteID, collection
		if err := rows.Scan(&d.ID, &d.DataJSON, &d.CreatedBy, &d.UpdatedBy, &d.CreatedAt, &d.UpdatedAt); err != nil {
			return nil, err
		}
		out = append(out, d)
	}
	return out, rows.Err()
}

func (s *Store) PutDocument(ctx context.Context, site, collection, id, dataJSON, actor string) (Document, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return Document{}, err
	}
	t := now()
	res, err := s.DB.ExecContext(ctx, `UPDATE documents SET data_json=?, updated_by=?, updated_at=? WHERE site_id=? AND collection=? AND id=?`, dataJSON, actor, t, siteID, collection, id)
	if err != nil {
		return Document{}, err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return Document{}, ErrNotFound
	}
	return s.GetDocument(ctx, site, collection, id)
}

func (s *Store) DeleteDocument(ctx context.Context, site, collection, id string) error {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return err
	}
	res, err := s.DB.ExecContext(ctx, `DELETE FROM documents WHERE site_id=? AND collection=? AND id=?`, siteID, collection, id)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) CreateUpload(ctx context.Context, site, id, path, contentType string, size int64, actor string) (Upload, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return Upload{}, err
	}
	t := now()
	_, err = s.DB.ExecContext(ctx, `INSERT INTO uploads(site_id, id, path, content_type, size, created_by, created_at) VALUES(?,?,?,?,?,?,?)`, siteID, id, path, contentType, size, actor, t)
	if err != nil {
		return Upload{}, err
	}
	return Upload{SiteID: siteID, ID: id, Path: path, ContentType: contentType, Size: size, CreatedBy: actor, CreatedAt: t}, nil
}

func (s *Store) GetUpload(ctx context.Context, site, id string) (Upload, error) {
	siteID, err := s.SiteID(ctx, site)
	if err != nil {
		return Upload{}, err
	}
	row := s.DB.QueryRowContext(ctx, `SELECT path, content_type, size, COALESCE(created_by,''), created_at FROM uploads WHERE site_id=? AND id=?`, siteID, id)
	var u Upload
	u.SiteID, u.ID = siteID, id
	if err := row.Scan(&u.Path, &u.ContentType, &u.Size, &u.CreatedBy, &u.CreatedAt); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return Upload{}, ErrNotFound
		}
		return Upload{}, err
	}
	return u, nil
}

func (s *Store) DeleteUpload(ctx context.Context, site, id string) (Upload, error) {
	u, err := s.GetUpload(ctx, site, id)
	if err != nil {
		return Upload{}, err
	}
	_, err = s.DB.ExecContext(ctx, `DELETE FROM uploads WHERE site_id=? AND id=?`, u.SiteID, id)
	if err != nil {
		return Upload{}, err
	}
	return u, nil
}

func (s *Store) Check() error {
	var one int
	if err := s.DB.QueryRow(`SELECT 1`).Scan(&one); err != nil {
		return fmt.Errorf("sqlite check: %w", err)
	}
	return nil
}
