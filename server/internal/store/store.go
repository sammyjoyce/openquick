package store

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

type Store struct {
	DB  *sql.DB
	DSN string
}

type SiteRecord struct {
	ID        int64  `json:"-"`
	Name      string `json:"name"`
	Subdomain string `json:"subdomain"`
	URL       string `json:"url"`
	Release   string `json:"release"`
	UpdatedAt string `json:"updated_at"`
	Deployer  string `json:"deployer"`
	Public    bool   `json:"public"`
}

type DeployAudit struct {
	Subdomain     string
	SSHUser       string
	SSHKeyID      string
	SSHPrincipals string
}

type DeployRecord struct {
	Site      string `json:"site"`
	Release   string `json:"release"`
	Deployer  string `json:"deployer"`
	CreatedAt string `json:"created_at"`
}

type DomainRecord struct {
	Domain    string `json:"domain"`
	Site      string `json:"site"`
	CreatedAt string `json:"created_at"`
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

type AIAudit struct {
	Site             string
	Subject          string
	Provider         string
	Model            string
	PromptTokens     int
	CompletionTokens int
	CreatedAt        string
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
	st := &Store{DB: db, DSN: path}
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
	st := &Store{DB: db, DSN: "file:quickd-test?mode=memory&cache=shared"}
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
			last_release_id TEXT,
			public INTEGER NOT NULL DEFAULT 0
		);`,
		`CREATE TABLE IF NOT EXISTS deploys (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			site_id INTEGER NOT NULL REFERENCES sites(id) ON DELETE CASCADE,
			release_id TEXT NOT NULL,
			deployer TEXT,
			bytes INTEGER NOT NULL,
			files INTEGER NOT NULL,
			created_at TEXT NOT NULL,
			ssh_user TEXT,
			ssh_key_id TEXT,
			ssh_principals TEXT
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
		`CREATE TABLE IF NOT EXISTS ai_audit (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			site TEXT NOT NULL,
			subject TEXT NOT NULL,
			provider TEXT NOT NULL,
			model TEXT NOT NULL,
			prompt_tokens INTEGER NOT NULL,
			completion_tokens INTEGER NOT NULL,
			created_at TEXT NOT NULL
		);`,
		`CREATE TABLE IF NOT EXISTS dev_tokens (
			site TEXT NOT NULL,
			token_hash TEXT NOT NULL,
			subject TEXT NOT NULL,
			expires_at TEXT NOT NULL,
			PRIMARY KEY(site, token_hash)
		);`,
		`CREATE TABLE IF NOT EXISTS domains (
			domain TEXT NOT NULL PRIMARY KEY,
			site TEXT NOT NULL,
			created_at TEXT NOT NULL,
			FOREIGN KEY(site) REFERENCES sites(name) ON DELETE CASCADE
		);`,
	}
	for _, stmt := range stmts {
		if _, err := s.DB.Exec(stmt); err != nil {
			return err
		}
	}
	if err := s.ensureColumn("sites", "subdomain", "TEXT"); err != nil {
		return err
	}
	if _, err := s.DB.Exec(`UPDATE sites SET subdomain=name WHERE subdomain IS NULL OR subdomain=''`); err != nil {
		return err
	}
	if _, err := s.DB.Exec(`CREATE UNIQUE INDEX IF NOT EXISTS idx_sites_subdomain_unique ON sites(subdomain)`); err != nil {
		return err
	}
	if err := s.ensureColumn("sites", "public", "INTEGER NOT NULL DEFAULT 0"); err != nil {
		return err
	}
	for _, col := range []struct{ name, def string }{
		{"ssh_user", "TEXT"},
		{"ssh_key_id", "TEXT"},
		{"ssh_principals", "TEXT"},
	} {
		if err := s.ensureColumn("deploys", col.name, col.def); err != nil {
			return err
		}
	}
	return nil
}

func (s *Store) ensureColumn(table, column, def string) error {
	rows, err := s.DB.Query(`PRAGMA table_info(` + table + `)`)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		var cid int
		var name, typ string
		var notnull int
		var dflt any
		var pk int
		if err := rows.Scan(&cid, &name, &typ, &notnull, &dflt, &pk); err != nil {
			return err
		}
		if name == column {
			return nil
		}
	}
	if err := rows.Err(); err != nil {
		return err
	}
	_, err = s.DB.Exec(`ALTER TABLE ` + table + ` ADD COLUMN ` + column + ` ` + def)
	return err
}

func now() string { return time.Now().UTC().Format(time.RFC3339Nano) }

func (s *Store) EnsureSite(ctx context.Context, name, subdomain string) (int64, error) {
	if subdomain == "" {
		subdomain = name
	}
	t := now()
	res, err := s.DB.ExecContext(ctx, `INSERT INTO sites(name, subdomain, created_at, updated_at) VALUES(?,?,?,?) ON CONFLICT(name) DO UPDATE SET subdomain=excluded.subdomain, updated_at=CASE WHEN sites.subdomain <> excluded.subdomain THEN excluded.updated_at ELSE sites.updated_at END`, name, subdomain, t, t)
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
	row := s.DB.QueryRowContext(ctx, `SELECT s.id, s.name, COALESCE(s.subdomain,s.name), COALESCE(s.last_release_id,''), s.updated_at, COALESCE((SELECT d.deployer FROM deploys d WHERE d.site_id=s.id ORDER BY d.created_at DESC, d.id DESC LIMIT 1),''), COALESCE(s.public,0) FROM sites s WHERE s.name = ?`, name)
	return scanSite(row)
}

func (s *Store) GetSiteBySubdomain(ctx context.Context, subdomain string) (SiteRecord, error) {
	row := s.DB.QueryRowContext(ctx, `SELECT s.id, s.name, COALESCE(s.subdomain,s.name), COALESCE(s.last_release_id,''), s.updated_at, COALESCE((SELECT d.deployer FROM deploys d WHERE d.site_id=s.id ORDER BY d.created_at DESC, d.id DESC LIMIT 1),''), COALESCE(s.public,0) FROM sites s WHERE s.subdomain = ? OR s.name = ? ORDER BY CASE WHEN s.subdomain = ? THEN 0 ELSE 1 END LIMIT 1`, subdomain, subdomain, subdomain)
	return scanSite(row)
}

func scanSite(row interface{ Scan(dest ...any) error }) (SiteRecord, error) {
	var rec SiteRecord
	var pub int
	if err := row.Scan(&rec.ID, &rec.Name, &rec.Subdomain, &rec.Release, &rec.UpdatedAt, &rec.Deployer, &pub); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return SiteRecord{}, ErrNotFound
		}
		return SiteRecord{}, err
	}
	rec.Public = pub != 0
	return rec, nil
}

func (s *Store) ListSites(ctx context.Context) ([]SiteRecord, error) {
	rows, err := s.DB.QueryContext(ctx, `SELECT s.id, s.name, COALESCE(s.subdomain,s.name), COALESCE(s.last_release_id,''), s.updated_at, COALESCE((SELECT d.deployer FROM deploys d WHERE d.site_id=s.id ORDER BY d.created_at DESC, d.id DESC LIMIT 1),''), COALESCE(s.public,0) FROM sites s ORDER BY s.name`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []SiteRecord
	for rows.Next() {
		rec, err := scanSite(rows)
		if err != nil {
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

func (s *Store) SetSiteSubdomain(ctx context.Context, site, subdomain string) error {
	res, err := s.DB.ExecContext(ctx, `UPDATE sites SET subdomain=?, updated_at=? WHERE name=?`, subdomain, now(), site)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) SetSitePublic(ctx context.Context, site string, public bool) error {
	v := 0
	if public {
		v = 1
	}
	res, err := s.DB.ExecContext(ctx, `UPDATE sites SET public=?, updated_at=? WHERE name=?`, v, now(), site)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) IsSitePublic(ctx context.Context, site string) (bool, error) {
	var v int
	if err := s.DB.QueryRowContext(ctx, `SELECT COALESCE(public,0) FROM sites WHERE name=?`, site).Scan(&v); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return false, ErrNotFound
		}
		return false, err
	}
	return v != 0, nil
}

func (s *Store) LastDeploy(ctx context.Context, site string) (DeployRecord, error) {
	row := s.DB.QueryRowContext(ctx, `SELECT s.name, d.release_id, COALESCE(d.deployer,''), d.created_at FROM deploys d JOIN sites s ON s.id=d.site_id WHERE s.name=? ORDER BY d.created_at DESC, d.id DESC LIMIT 1`, site)
	var rec DeployRecord
	if err := row.Scan(&rec.Site, &rec.Release, &rec.Deployer, &rec.CreatedAt); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return DeployRecord{}, ErrNotFound
		}
		return DeployRecord{}, err
	}
	return rec, nil
}

func (s *Store) RecordDeploy(ctx context.Context, site, release, deployer string, bytes int64, files int, opts ...DeployAudit) error {
	var audit DeployAudit
	if len(opts) > 0 {
		audit = opts[0]
	}
	tx, err := s.DB.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	t := now()
	var siteID int64
	var currentSubdomain string
	err = tx.QueryRowContext(ctx, `SELECT id, COALESCE(subdomain,name) FROM sites WHERE name = ?`, site).Scan(&siteID, &currentSubdomain)
	if errors.Is(err, sql.ErrNoRows) {
		if audit.Subdomain == "" {
			audit.Subdomain = site
		}
		res, err := tx.ExecContext(ctx, `INSERT INTO sites(name, subdomain, created_at, updated_at, last_release_id) VALUES(?,?,?,?,?)`, site, audit.Subdomain, t, t, release)
		if err != nil {
			return err
		}
		siteID, err = res.LastInsertId()
		if err != nil {
			return err
		}
	} else if err != nil {
		return err
	} else {
		if audit.Subdomain == "" {
			audit.Subdomain = currentSubdomain
		}
		if _, err := tx.ExecContext(ctx, `UPDATE sites SET subdomain=?, last_release_id=?, updated_at=? WHERE id=?`, audit.Subdomain, release, t, siteID); err != nil {
			return err
		}
	}
	_, err = tx.ExecContext(ctx, `INSERT INTO deploys(site_id, release_id, deployer, bytes, files, created_at, ssh_user, ssh_key_id, ssh_principals) VALUES(?,?,?,?,?,?,?,?,?)`, siteID, release, deployer, bytes, files, t, audit.SSHUser, audit.SSHKeyID, audit.SSHPrincipals)
	if err != nil {
		return err
	}
	return tx.Commit()
}

func (s *Store) AddDomain(ctx context.Context, domain, site string) (DomainRecord, error) {
	if _, err := s.SiteID(ctx, site); err != nil {
		return DomainRecord{}, err
	}
	t := now()
	_, err := s.DB.ExecContext(ctx, `INSERT INTO domains(domain, site, created_at) VALUES(?,?,?)`, domain, site, t)
	if err != nil {
		return DomainRecord{}, err
	}
	return DomainRecord{Domain: domain, Site: site, CreatedAt: t}, nil
}

func (s *Store) RemoveDomain(ctx context.Context, domain string) error {
	res, err := s.DB.ExecContext(ctx, `DELETE FROM domains WHERE domain=?`, domain)
	if err != nil {
		return err
	}
	n, _ := res.RowsAffected()
	if n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) ListDomains(ctx context.Context) ([]DomainRecord, error) {
	rows, err := s.DB.QueryContext(ctx, `SELECT domain, site, created_at FROM domains ORDER BY domain`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []DomainRecord
	for rows.Next() {
		var rec DomainRecord
		if err := rows.Scan(&rec.Domain, &rec.Site, &rec.CreatedAt); err != nil {
			return nil, err
		}
		out = append(out, rec)
	}
	return out, rows.Err()
}

func (s *Store) SiteForDomain(ctx context.Context, domain string) (string, error) {
	var site string
	if err := s.DB.QueryRowContext(ctx, `SELECT site FROM domains WHERE domain=?`, domain).Scan(&site); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return "", ErrNotFound
		}
		return "", err
	}
	return site, nil
}

func (s *Store) RecentDeploys(ctx context.Context, limit int) ([]DeployRecord, error) {
	if limit <= 0 {
		limit = 10
	}
	rows, err := s.DB.QueryContext(ctx, `SELECT s.name, d.release_id, COALESCE(d.deployer,''), d.created_at FROM deploys d JOIN sites s ON s.id=d.site_id ORDER BY d.created_at DESC, d.id DESC LIMIT ?`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []DeployRecord
	for rows.Next() {
		var rec DeployRecord
		if err := rows.Scan(&rec.Site, &rec.Release, &rec.Deployer, &rec.CreatedAt); err != nil {
			return nil, err
		}
		out = append(out, rec)
	}
	return out, rows.Err()
}

func (s *Store) CountSites(ctx context.Context) (int, error) {
	var n int
	return n, s.DB.QueryRowContext(ctx, `SELECT COUNT(*) FROM sites`).Scan(&n)
}

func (s *Store) CountReleases(ctx context.Context) (int, error) {
	var n int
	return n, s.DB.QueryRowContext(ctx, `SELECT COUNT(*) FROM deploys`).Scan(&n)
}

func (s *Store) RateLimitCounts(ctx context.Context) (map[string]int, error) {
	rows, err := s.DB.QueryContext(ctx, `SELECT scope, SUM(count) FROM rate_limits GROUP BY scope ORDER BY scope`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := map[string]int{}
	for rows.Next() {
		var scope string
		var count int
		if err := rows.Scan(&scope, &count); err != nil {
			return nil, err
		}
		out[scope] = count
	}
	return out, rows.Err()
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

func (s *Store) RecordAIAudit(ctx context.Context, site, subject, provider, model string, promptTokens, completionTokens int) error {
	if s == nil || s.DB == nil {
		return errors.New("store unavailable")
	}
	_, err := s.DB.ExecContext(ctx, `INSERT INTO ai_audit(site, subject, provider, model, prompt_tokens, completion_tokens, created_at) VALUES(?,?,?,?,?,?,?)`, site, subject, provider, model, promptTokens, completionTokens, now())
	return err
}

func (s *Store) AIAuditCount(ctx context.Context, site string) (int, error) {
	var n int
	err := s.DB.QueryRowContext(ctx, `SELECT COUNT(*) FROM ai_audit WHERE site=?`, site).Scan(&n)
	return n, err
}

func (s *Store) AllowRateLimit(ctx context.Context, scope, key string, windowStart int64, limit int) (bool, error) {
	if limit <= 0 {
		return true, nil
	}
	tx, err := s.DB.BeginTx(ctx, nil)
	if err != nil {
		return false, err
	}
	defer tx.Rollback()
	var count int
	err = tx.QueryRowContext(ctx, `SELECT count FROM rate_limits WHERE scope=? AND key=? AND window_start=?`, scope, key, windowStart).Scan(&count)
	if errors.Is(err, sql.ErrNoRows) {
		if _, err := tx.ExecContext(ctx, `INSERT INTO rate_limits(scope, key, window_start, count) VALUES(?,?,?,1)`, scope, key, windowStart); err != nil {
			return false, err
		}
		return true, tx.Commit()
	}
	if err != nil {
		return false, err
	}
	if count >= limit {
		return false, tx.Commit()
	}
	if _, err := tx.ExecContext(ctx, `UPDATE rate_limits SET count=count+1 WHERE scope=? AND key=? AND window_start=?`, scope, key, windowStart); err != nil {
		return false, err
	}
	return true, tx.Commit()
}

const devTokenTimeFormat = "2006-01-02T15:04:05.000000000Z07:00"

func (s *Store) MintDevToken(ctx context.Context, site, subject string, ttl time.Duration) (string, string, error) {
	if ttl <= 0 {
		return "", "", errors.New("ttl must be positive")
	}
	if _, err := s.SiteID(ctx, site); err != nil {
		return "", "", err
	}
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", "", err
	}
	token := base64.RawURLEncoding.EncodeToString(b[:])
	expiresAt := time.Now().UTC().Add(ttl).Format(devTokenTimeFormat)
	if subject == "" {
		subject = "admin"
	}
	if _, err := s.DB.ExecContext(ctx, `INSERT INTO dev_tokens(site, token_hash, subject, expires_at) VALUES(?,?,?,?)`, site, devTokenHash(token), subject, expiresAt); err != nil {
		return "", "", err
	}
	return token, expiresAt, nil
}

func (s *Store) ValidateDevToken(ctx context.Context, site, token string, at time.Time) (string, bool, error) {
	if s == nil || s.DB == nil || token == "" {
		return "", false, nil
	}
	nowStr := at.UTC().Format(devTokenTimeFormat)
	if _, err := s.DB.ExecContext(ctx, `DELETE FROM dev_tokens WHERE expires_at <= ?`, nowStr); err != nil {
		return "", false, err
	}
	var subject string
	err := s.DB.QueryRowContext(ctx, `SELECT subject FROM dev_tokens WHERE site=? AND token_hash=? AND expires_at>?`, site, devTokenHash(token), nowStr).Scan(&subject)
	if errors.Is(err, sql.ErrNoRows) {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	return subject, true, nil
}

func devTokenHash(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:])
}

func (s *Store) Check() error {
	var one int
	if err := s.DB.QueryRow(`SELECT 1`).Scan(&one); err != nil {
		return fmt.Errorf("sqlite check: %w", err)
	}
	return nil
}
