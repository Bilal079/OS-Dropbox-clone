#!/usr/bin/env bash
set -euo pipefail

PORT=${PORT:-9000}
ROOT=${ROOT:-storage}
QUOTA=${QUOTA:-104857600}

mkdir -p "$ROOT"

echo "Running server under Valgrind on port $PORT (root=$ROOT quota=$QUOTA)"
exec valgrind \
  --leak-check=full --show-leak-kinds=all --track-origins=yes \
  --errors-for-leak-kinds=definite,indirect,possible --error-exitcode=1 \
  ./bin/server --port "$PORT" --root "$ROOT" --quota-bytes "$QUOTA"


