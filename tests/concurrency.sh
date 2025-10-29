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

./bin/client --host 127.0.0.1 --port "$PORT" signup u1 p1 || true
./bin/client --host 127.0.0.1 --port "$PORT" signup u2 p2 || true

jobs_pids=()

for u in u1 u2; do
  for i in $(seq 1 5); do
    (
      tmpf=$(mktemp)
      dd if=/dev/urandom of="$tmpf" bs=4096 count=32 status=none
      name=$(basename "$tmpf")
      outfile="/tmp/${name}.out"
      ./bin/client --host 127.0.0.1 --port "$PORT" <<EOF
login $u p${u:1}
upload $tmpf
list
download $name $outfile
delete $name
quit
EOF
      rm -f "$tmpf" "$outfile"
    ) &
    jobs_pids+=($!)
  done
done

for p in "${jobs_pids[@]}"; do wait "$p"; done

echo "CONCURRENCY OK"


