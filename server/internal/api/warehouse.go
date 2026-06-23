package api

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
)

type warehouseQueryMetadata struct {
	Name    string                          `json:"name"`
	Params  []config.WarehouseParamConfig   `json:"params"`
	MaxRows int                             `json:"max_rows"`
	Columns []string                        `json:"columns"`
}

type warehouseResult struct {
	Name      string   `json:"name"`
	Columns   []string `json:"columns"`
	Rows      [][]any  `json:"rows"`
	RowCount  int      `json:"row_count"`
	Truncated bool     `json:"truncated"`
}

func (s *Server) handleWarehouse(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if !s.Config.WarehouseConfigured() {
		http.Error(w, "warehouse disabled", http.StatusServiceUnavailable)
		return
	}
	if !s.dataAllowed(r, id) {
		http.Error(w, "authentication required", http.StatusUnauthorized)
		return
	}
	if r.Method != http.MethodGet && r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	name := strings.Trim(strings.TrimPrefix(r.URL.Path, "/_quick/warehouse"), "/")
	if name == "" {
		if r.Method != http.MethodGet {
			methodNotAllowed(w)
			return
		}
		meta, err := s.warehouseMetadata(r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"queries": meta})
		return
	}
	if !idRE.MatchString(name) {
		http.NotFound(w, r)
		return
	}
	query, ok := s.warehouseQuery(name)
	if !ok {
		http.NotFound(w, r)
		return
	}
	params, err := warehouseParams(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	args, err := bindWarehouseParams(query.Params, params)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	out, err := s.runWarehouseQuery(r, query, args)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	writeJSON(w, http.StatusOK, out)
	_ = site
}

func (s *Server) warehouseQuery(name string) (config.WarehouseQueryConfig, bool) {
	for _, q := range s.Config.Warehouse.Queries {
		if q.Name == name {
			return q, true
		}
	}
	return config.WarehouseQueryConfig{}, false
}

func (s *Server) warehouseMetadata(r *http.Request) ([]warehouseQueryMetadata, error) {
	out := make([]warehouseQueryMetadata, 0, len(s.Config.Warehouse.Queries))
	for _, q := range s.Config.Warehouse.Queries {
		cols, err := s.warehouseColumns(r, q)
		if err != nil {
			cols = []string{}
		}
		out = append(out, warehouseQueryMetadata{Name: q.Name, Params: q.Params, MaxRows: q.MaxRows, Columns: cols})
	}
	return out, nil
}

func (s *Server) warehouseColumns(r *http.Request, q config.WarehouseQueryConfig) ([]string, error) {
	if s.Store == nil || s.Store.DB == nil {
		return nil, errors.New("store unavailable")
	}
	sqlText, err := config.CleanWarehouseSQL(q.SQL)
	if err != nil {
		return nil, err
	}
	args := make([]any, 0, len(q.Params))
	for _, p := range q.Params {
		switch p.Type {
		case "int":
			args = append(args, 0)
		case "float":
			args = append(args, 0.0)
		default:
			args = append(args, "")
		}
	}
	rows, err := s.Store.DB.QueryContext(r.Context(), "SELECT * FROM ("+sqlText+") LIMIT 0", args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	return rows.Columns()
}

func warehouseParams(r *http.Request) (map[string]any, error) {
	if r.Method == http.MethodGet {
		out := map[string]any{}
		for key, values := range r.URL.Query() {
			if len(values) > 0 {
				out[key] = values[0]
			}
		}
		return out, nil
	}
	data, err := readAllCapped(r.Body, jsonLimit)
	if err != nil {
		return nil, err
	}
	data = bytes.TrimSpace(data)
	if len(data) == 0 {
		return map[string]any{}, nil
	}
	var out map[string]any
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(&out); err != nil {
		return nil, err
	}
	if out == nil {
		return nil, errors.New("JSON object required")
	}
	return out, nil
}

func bindWarehouseParams(specs []config.WarehouseParamConfig, params map[string]any) ([]any, error) {
	args := make([]any, 0, len(specs))
	for _, spec := range specs {
		raw, ok := params[spec.Name]
		if !ok {
			return nil, fmt.Errorf("missing param %q", spec.Name)
		}
		switch strings.ToLower(spec.Type) {
		case "string":
			s, ok := raw.(string)
			if !ok {
				return nil, fmt.Errorf("param %q must be string", spec.Name)
			}
			args = append(args, s)
		case "int":
			v, err := asInt64(raw)
			if err != nil {
				return nil, fmt.Errorf("param %q must be int", spec.Name)
			}
			args = append(args, v)
		case "float":
			v, err := asFloat64(raw)
			if err != nil {
				return nil, fmt.Errorf("param %q must be float", spec.Name)
			}
			args = append(args, v)
		default:
			return nil, fmt.Errorf("unsupported param type %q", spec.Type)
		}
	}
	return args, nil
}

func asInt64(v any) (int64, error) {
	switch x := v.(type) {
	case json.Number:
		return x.Int64()
	case string:
		return strconv.ParseInt(x, 10, 64)
	case float64:
		if x != float64(int64(x)) {
			return 0, errors.New("not an integer")
		}
		return int64(x), nil
	case int:
		return int64(x), nil
	case int64:
		return x, nil
	default:
		return 0, errors.New("not an integer")
	}
}

func asFloat64(v any) (float64, error) {
	switch x := v.(type) {
	case json.Number:
		return x.Float64()
	case string:
		return strconv.ParseFloat(x, 64)
	case float64:
		return x, nil
	case int:
		return float64(x), nil
	case int64:
		return float64(x), nil
	default:
		return 0, errors.New("not a float")
	}
}

func (s *Server) runWarehouseQuery(r *http.Request, query config.WarehouseQueryConfig, args []any) (warehouseResult, error) {
	if s.Store == nil || s.Store.DSN == "" {
		return warehouseResult{}, errors.New("store unavailable")
	}
	sqlText, err := config.CleanWarehouseSQL(query.SQL)
	if err != nil {
		return warehouseResult{}, err
	}
	db, err := sql.Open("sqlite", s.Store.DSN)
	if err != nil {
		return warehouseResult{}, err
	}
	defer db.Close()
	db.SetMaxOpenConns(1)
	if _, err := db.ExecContext(r.Context(), `PRAGMA query_only=ON`); err != nil {
		return warehouseResult{}, err
	}
	if _, err := db.ExecContext(r.Context(), `PRAGMA busy_timeout=5000`); err != nil {
		return warehouseResult{}, err
	}
	rows, err := db.QueryContext(r.Context(), sqlText, args...)
	if err != nil {
		return warehouseResult{}, err
	}
	defer rows.Close()
	columns, err := rows.Columns()
	if err != nil {
		return warehouseResult{}, err
	}
	maxRows := query.MaxRows
	if maxRows <= 0 {
		maxRows = 1000
	}
	out := warehouseResult{Name: query.Name, Columns: columns, Rows: [][]any{}}
	for rows.Next() {
		vals := make([]any, len(columns))
		dest := make([]any, len(columns))
		for i := range vals {
			dest[i] = &vals[i]
		}
		if err := rows.Scan(dest...); err != nil {
			return warehouseResult{}, err
		}
		if len(out.Rows) >= maxRows {
			out.Truncated = true
			break
		}
		for i, v := range vals {
			if b, ok := v.([]byte); ok {
				vals[i] = string(b)
			}
		}
		out.Rows = append(out.Rows, vals)
	}
	if err := rows.Err(); err != nil {
		return warehouseResult{}, err
	}
	out.RowCount = len(out.Rows)
	return out, nil
}
