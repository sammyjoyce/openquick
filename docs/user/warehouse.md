# Warehouse queries

OpenQuick warehouse access is disabled by default. Hosts advertise `capabilities().warehouse === true` only when an operator configures named queries for the site environment.

Warehouse queries are intended for reports and dashboards that need server-side credentials without exposing database URLs or SQL to browser code.

## Host configuration

Configure data sources and named queries on the host. Keep connection strings server-side and expose only query names plus typed parameters:

```json
{
  "warehouse": {
    "enabled": true,
    "sources": [
      {
        "name": "analytics",
        "driver": "postgres",
        "url_env": "ANALYTICS_DATABASE_URL"
      }
    ],
    "queries": {
      "recent_orders": {
        "source": "analytics",
        "sql": "select id, email, total_cents, created_at from orders where status = :status order by created_at desc limit :limit",
        "params": {
          "status": { "type": "string", "default": "paid" },
          "limit": { "type": "integer", "default": 25, "max": 100 }
        },
        "allow_identities": ["group:finance"],
        "max_rows": 100
      }
    }
  }
}
```

Operational guidance:

- `enabled` defaults to `false`.
- Query names are an allowlist; browser users cannot submit arbitrary SQL.
- Use bound parameters only. Do not concatenate user input into SQL strings.
- Set row limits and mark truncated results rather than streaming unbounded tables.
- Store database credentials in environment variables or a server-side secret store.
- Match `allow_identities` to the identity provider fields the host trusts.

## SDK usage

Import the SDK from the same origin:

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';
</script>
```

Run a named query with JSON parameters:

```js
const report = await quick.warehouse.query('recent_orders', {
  status: 'paid',
  limit: 20,
});

const rows = report.rows.map((row) =>
  Object.fromEntries(report.columns.map((column, index) => [column, row[index]])),
);

renderTable(rows);
```

Response shape:

```json
{
  "name": "recent_orders",
  "columns": ["id", "email", "total_cents", "created_at"],
  "rows": [["ord_123", "sam@example.com", 4200, "2026-06-12T10:00:00Z"]],
  "row_count": 1,
  "truncated": false
}
```

If the host does not advertise warehouse support, the SDK fails before sending query parameters with `quick.warehouse.query is not available on this host`.

## HTTP backing

The SDK uses same-origin `POST /_quick/warehouse/:name` with a JSON object body. Hosts may also support `GET /_quick/warehouse/:name` with query
parameters for simple links, but SDK code should use `quick.warehouse.query()` so capability checks and response handling stay consistent.
