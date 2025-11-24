#!/usr/bin/env bash
set -euo pipefail

# Small helper to POST a large body (default 100M) to a URL.
# Usage:
#   ./scripts/post_100mb.sh [URL] [SIZE_M]
# Examples:
#   ./scripts/post_100mb.sh http://127.0.0.1:8080/upload 100
#   ./scripts/post_100mb.sh http://127.0.0.1:8080/upload

URL="${1:-http://127.0.0.1:8080/upload}"
SIZE_M_STR="${2:-100}"

if ! [[ "$SIZE_M_STR" =~ ^[0-9]+$ ]]; then
  echo "SIZE_M must be an integer number of megabytes (e.g. 100)" >&2
  exit 1
fi

SIZE_M="$SIZE_M_STR"

TMP_FILE="$(mktemp -t post_body_XXXXXX.bin)"
trap 'rm -f "$TMP_FILE"' EXIT

echo "Generating ${SIZE_M}M payload at $TMP_FILE ..."
if command -v fallocate >/dev/null 2>&1; then
  # Create a sparse file quickly; curl will still read SIZE bytes
  fallocate -l "${SIZE_M}M" "$TMP_FILE"
else
  # Portable fallback
  dd if=/dev/zero of="$TMP_FILE" bs=1M count="$SIZE_M" status=none
fi

BYTES=$(stat -c%s "$TMP_FILE")
echo "Payload size: ${BYTES} bytes"

echo "POSTing to $URL ..."
set +e
HTTP_OUTPUT=$(curl -sS -o /dev/null \
  -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$TMP_FILE" \
  -w "code=%{http_code} uploaded=%{size_upload}B time_total=%{time_total}s speed=%{speed_upload}B/s" \
  "$URL")
STATUS=$?
set -e

if [ $STATUS -ne 0 ]; then
  echo "curl failed with status $STATUS" >&2
  exit $STATUS
fi

echo "$HTTP_OUTPUT"

case "$HTTP_OUTPUT" in
  *"code=200"*) ;;
  *"code=201"*) ;;
  *) echo "Warning: non-success HTTP response" >&2 ;;
esac


