Build
 - Prereqs: gcc/clang, pthreads, SQLite3 dev lib
 - Build: `make`
 - Debug/TSan: `make debug`, `make tsan`
 - Valgrind: `make valgrind`

Run Server
 - `./bin/server`

Client Usage
 - `./bin/client`
 
 - Then type commands:
   - `signup <user> <pass>`
   - `login <user> <pass>`
   - `upload <local_path>`
   - `list`
   - `download <name> <out_path>`
   - `delete <name>`
   - `quit`

Notes
 - Upload creates a test file locally if the path does not exist.
 - Protocol: SIGNUP/LOGIN handled by client threads; file ops via worker pool.
 - Use Valgrind/TSan targets to check leaks and races.

Valgrind
 - `make valgrind`     #runs server under Valgrind
 - `PORT=9001 ROOT=storage QUOTA=104857600 bash tests/valgrind_server.sh`

 Complete guide
  - Install deps:
    - `sudo apt update`
    - `sudo apt install -y build-essential sqlite3 libsqlite3-dev valgrind`
  - Build: `make`
  - Start server: `./bin/server` or `make valgrind`
  - In another terminal, run client: `./bin/client`
  - Try: `signup u1 p1`, `login u1 p1`, `upload ./a.txt`, `list`, `download a.txt ./a.out`, `delete a.txt`, `quit`
  - Stop server with Ctrl+C and review output (Valgrind shows leak summary)

Tests
 - Smoke: `make test` or `make smoke` (starts server, runs an end-to-end sequence)
 - Concurrency: `make concurrency` (spawns multiple interactive clients concurrently)
   - Increase load by editing `tests/concurrency.sh` inner loop `seq 1 5`
   - With ThreadSanitizer: `make tsan`
  - TSAN automated run: `make tsan-test` (builds with TSAN, runs concurrency, prints a summary)