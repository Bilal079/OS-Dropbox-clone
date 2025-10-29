#!/usr/bin/env bash
set -euo pipefail

ROOT=${ROOT:-storage}
PORT=${PORT:-9000}

echo "Starting server..."
./bin/server --port "$PORT" --root "$ROOT" &
SVR_PID=$!
sleep 1

cleanup(){
  kill $SVR_PID 2>/dev/null || true
  wait $SVR_PID 2>/dev/null || true
}
trap cleanup EXIT

./bin/client --host 127.0.0.1 --port "$PORT" <<'CMDS'
signup u1 p1
login u1 p1
upload ./a.txt
list
download a.txt ./a.out
delete a.txt
list
quit
CMDS

echo "OK"


