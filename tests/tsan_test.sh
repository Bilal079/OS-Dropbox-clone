#!/usr/bin/env bash
set -euo pipefail

#Build normal binaries first (non-TSAN)
make clean >/dev/null 2>&1 || true
make >/dev/null

#Rebuild server ONLY with TSAN flags; keep client non-TSAN
make CFLAGS="-Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread" \
     LDFLAGS="-fsanitize=thread -pthread" \
     bin/server >/dev/null

#Run server under TSAN only and drive concurrency workload from normal clients
LOG_FILE=$(mktemp)
PORT=${PORT:-9000}
ROOT=${ROOT:-storage}
QUOTA=${QUOTA:-104857600}

echo "Running TSAN server with concurrency workload..."
#Prefer disabling ASLR to avoid TSAN 'unexpected memory mapping' on some kernels
SETARCH_PREFIX=""
if command -v setarch >/dev/null 2>&1; then
  SETARCH_PREFIX="setarch $(uname -m) -R"
fi
TSAN_OPTIONS="halt_on_error=1 memory_limit_mb=8192 report_signal_unsafe=0" \
  $SETARCH_PREFIX ./bin/server --port "$PORT" --root "$ROOT" --quota-bytes "$QUOTA" >"$LOG_FILE" 2>&1 &
SVR_PID=$!
sleep 1

cleanup(){
  kill $SVR_PID 2>/dev/null || true
  wait $SVR_PID 2>/dev/null || true
}
trap cleanup EXIT

#Pre-create users (ignore if already exist)
./bin/client --host 127.0.0.1 --port "$PORT" signup u1 p1 || true
./bin/client --host 127.0.0.1 --port "$PORT" signup u2 p2 || true

#Launch concurrent interactive client sessions
jobs_pids=()
for u in u1 u2; do
  for i in $(seq 1 3); do
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

echo "CONCURRENCY OK" >>"$LOG_FILE"
TEST_RC=0

#Analyze log for ThreadSanitizer reports
ISSUE_COUNT=$(grep -c "ThreadSanitizer:" "$LOG_FILE" || true)

echo "============== TSAN SUMMARY =============="
if grep -q "unexpected memory mapping" "$LOG_FILE"; then
  echo "Result: FAIL (TSAN runtime failed: unexpected memory mapping)"
  echo "Hint: ensure 64-bit env; try disabling ASLR (setarch $(uname -m) -R)."
  echo "-----------------------------------------"
  tail -n 20 "$LOG_FILE" || true
  echo "Full log at: $LOG_FILE"
  exit 1
elif [[ "$ISSUE_COUNT" -gt 0 ]]; then
  echo "Result: FAIL (ThreadSanitizer reported $ISSUE_COUNT issue(s))"
  echo "-- First occurrence --"
  grep -n "ThreadSanitizer:" -n "$LOG_FILE" | head -1 | sed -E 's/^[^:]+://'
  echo "-----------------------------------------"
  echo "Full log at: $LOG_FILE"
  exit 1
else
  if grep -q "CONCURRENCY OK" "$LOG_FILE"; then
    echo "Result: PASS (no TSAN issues; concurrency OK)"
  else
    echo "Result: PASS (no TSAN issues)"
  fi
  echo "-----------------------------------------"
  tail -n 10 "$LOG_FILE" || true
  rm -f "$LOG_FILE"
  exit "$TEST_RC"
fi


