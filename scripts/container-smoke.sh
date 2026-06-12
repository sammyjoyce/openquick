#!/usr/bin/env bash
set -euo pipefail

IMAGE=${IMAGE:-openquick:test}
NAME=${NAME:-oq-test}
PORT=${PORT:-9366}
DOCKER=${DOCKER:-docker}

log() { printf '\n==> %s\n' "$*"; }
pass() { printf 'PASS %s\n' "$*"; }
fail() { printf 'FAIL %s\n' "$*" >&2; exit 1; }

json_field() {
  local field=$1
  sed -n "s/.*\"$field\":\"\([^\"]*\)\".*/\1/p"
}

curl_host() {
  local host=$1 path=$2
  curl -fsS -H "Host: $host" "http://127.0.0.1:${PORT}${path}"
}

cleanup() {
  set +e
  if "$DOCKER" ps -a --format '{{.Names}}' | grep -qx "$NAME"; then
    log "docker logs $NAME"
    "$DOCKER" logs "$NAME" 2>&1 | tail -n 80 || true
    "$DOCKER" rm -f "$NAME" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

log "start container"
"$DOCKER" rm -f "$NAME" >/dev/null 2>&1 || true
cid=$("$DOCKER" run -d --name "$NAME" -p "127.0.0.1:${PORT}:9366" "$IMAGE")
printf 'container=%s\n' "$cid"

log "wait for health"
for i in $(seq 1 60); do
  if health=$(curl -fsS "http://127.0.0.1:${PORT}/_quick/health" 2>/dev/null); then
    printf '%s\n' "$health"
    echo "$health" | grep -q '"ok":true' || fail "health body missing ok"
    pass "health"
    break
  fi
  if [ "$i" -eq 60 ]; then
    fail "health timeout"
  fi
  sleep 1
done

log "admin deploy prepare + activate"
prepare=$("$DOCKER" exec "$NAME" quickd deploy prepare --site demo --root /srv/quick --json)
printf '%s\n' "$prepare"
deploy_id=$(printf '%s' "$prepare" | json_field deploy_id)
staging=$(printf '%s' "$prepare" | json_field staging_path)
[ -n "$deploy_id" ] || fail "prepare did not return deploy_id"
[ -n "$staging" ] || fail "prepare did not return staging_path"
"$DOCKER" exec "$NAME" sh -c "printf '%s\n' '<!doctype html><h1>OpenQuick container demo</h1>' > '$staging/index.html'"
activate=$("$DOCKER" exec "$NAME" quickd deploy activate --site demo --deploy-id "$deploy_id" --root /srv/quick --json)
printf '%s\n' "$activate"
printf '%s' "$activate" | grep -q '"files":1' || fail "activate did not report one file"
pass "manual deploy"

log "static site and APIs"
page=$(curl_host demo.localhost /)
printf '%s\n' "$page"
printf '%s' "$page" | grep -q 'OpenQuick container demo' || fail "demo page mismatch"
pass "site root"

caps=$(curl_host demo.localhost /_quick/capabilities)
printf '%s\n' "$caps"
printf '%s' "$caps" | grep -q '"ai":false' || fail "capabilities ai not false"
printf '%s' "$caps" | grep -q '"warehouse":false' || fail "capabilities warehouse not false"
pass "capabilities"

sdk_full=$(curl_host demo.localhost /_quick/sdk.js)
sdk=$(printf '%s' "$sdk_full" | head -c 160)
printf '%s\n' "$sdk"
printf '%s' "$sdk_full" | grep -Eq 'export|quick' || fail "sdk.js sanity failed"
pass "sdk.js"

created=$(curl -fsS -X POST -H 'Host: demo.localhost' -H 'Content-Type: application/json' --data '{"text":"hello"}' "http://127.0.0.1:${PORT}/_quick/db/notes")
printf '%s\n' "$created"
note_id=$(printf '%s' "$created" | json_field id)
[ -n "$note_id" ] || fail "db create did not return id"
notes=$(curl_host demo.localhost /_quick/db/notes)
printf '%s\n' "$notes"
printf '%s' "$notes" | grep -q "$note_id" || fail "db list missing created note"
pass "db create/list"

upload=$(curl -fsS -X POST -H 'Host: demo.localhost' -H 'Content-Type: text/plain' --data 'upload-body' "http://127.0.0.1:${PORT}/_quick/uploads?name=note.txt")
printf '%s\n' "$upload"
up_id=$(printf '%s' "$upload" | json_field id)
[ -n "$up_id" ] || fail "upload did not return id"
up_body=$(curl_host demo.localhost "/_quick/uploads/${up_id}")
printf '%s\n' "$up_body"
printf '%s' "$up_body" | grep -q 'upload-body' || fail "upload get mismatch"
pass "uploads post/get"

fallback=$(curl_host localhost /~/demo/)
printf '%s\n' "$fallback"
printf '%s' "$fallback" | grep -q 'OpenQuick container demo' || fail "path fallback mismatch"
pass "path fallback"

directory=$(curl_host localhost /)
printf '%s\n' "$directory" | head -n 20
printf '%s' "$directory" | grep -q 'demo' || fail "apex directory missing demo"
pass "apex directory"

qdl=$("$DOCKER" exec "$NAME" quickd list --root /srv/quick --json)
printf '%s\n' "$qdl"
printf '%s' "$qdl" | grep -q '"name":"demo"' || fail "quickd list missing demo"
pass "quickd list"

log "quick CLI inside container"
cli=$("$DOCKER" exec "$NAME" sh -c 'set -e; quick --version; rm -rf /tmp/site2; quick init /tmp/site2; test -f /tmp/site2/quick.json; cd /tmp/site2; quick list --json; quick opencli | head -n 5')
printf '%s\n' "$cli"
printf '%s' "$cli" | grep -q 'OpenQuick' || fail "quick --version/opencli output sanity failed"
pass "quick CLI"

log "zip deploy path"
prep2=$("$DOCKER" exec "$NAME" quickd deploy prepare --site zipdemo --root /srv/quick --json)
printf '%s\n' "$prep2"
deploy_id2=$(printf '%s' "$prep2" | json_field deploy_id)
[ -n "$deploy_id2" ] || fail "zip prepare did not return deploy_id"
"$DOCKER" exec "$NAME" sh -c "rm -rf /tmp/zipsite /tmp/zipsite.zip; mkdir -p /tmp/zipsite; printf '%s\n' '<!doctype html><h1>Zip deploy works</h1>' > /tmp/zipsite/index.html; cd /tmp/zipsite; zip -q -r /tmp/zipsite.zip ."
extract=$("$DOCKER" exec "$NAME" quickd deploy extract-zip --site zipdemo --deploy-id "$deploy_id2" --zip /tmp/zipsite.zip --root /srv/quick --json)
printf '%s\n' "$extract"
printf '%s' "$extract" | grep -q '"extracted":true' || fail "extract zip not true"
act2=$("$DOCKER" exec "$NAME" quickd deploy activate --site zipdemo --deploy-id "$deploy_id2" --root /srv/quick --json)
printf '%s\n' "$act2"
zip_page=$(curl_host zipdemo.localhost /)
printf '%s\n' "$zip_page"
printf '%s' "$zip_page" | grep -q 'Zip deploy works' || fail "zip page mismatch"
pass "zip deploy"

log "negative checks"
trav_code=$(curl --path-as-is -sS -o /tmp/oq-traversal-body -w '%{http_code}' -H 'Host: demo.localhost' "http://127.0.0.1:${PORT}/../etc/passwd")
printf 'traversal_status=%s\n' "$trav_code"
case "$trav_code" in 400|404) pass "path traversal rejected" ;; *) fail "path traversal returned $trav_code" ;; esac
unknown_code=$(curl -sS -o /tmp/oq-unknown-body -w '%{http_code}' -H 'Host: unknown.localhost' "http://127.0.0.1:${PORT}/")
printf 'unknown_status=%s\n' "$unknown_code"
[ "$unknown_code" = "404" ] || fail "unknown site returned $unknown_code"
pass "unknown site 404"

log "smoke complete"
pass "container smoke"
