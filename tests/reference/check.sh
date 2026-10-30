#!/bin/bash
# Regression check against the published results.
#
#   tests/reference/check.sh <path to RISCV_VP built with TIMING_MODEL=CYCLE6_AXI> [jobs]
#
# Runs every program in reference.csv with no VP_* overrides, requires the
# VP's timed-region cycles and instructions to equal the recorded values, and
# prints the cycle difference to the recorded CVA6 RTL run. Exit status 1 if
# any VP number changed.
#
# rtl_* columns: CVA6 RTL co-sim (commit 6a4af75), mcycle/minstret of the
# timed region. vp_* columns: the same ELF on the VP, written to the result
# port (0x90000018) by the program itself.
set -euo pipefail
VP=$(realpath "${1:?usage: check.sh <RISCV_VP> [jobs]}")
JOBS=${2:-4}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$(dirname "$VP")
LIBS="$BUILD:$BUILD/systemc/src:$BUILD/spdlog"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

tail -n +2 "$ROOT/tests/reference/reference.csv" | cut -d, -f1,3 | tr , ' ' |
  xargs -P "$JOBS" -L1 sh -c '
    mkdir -p "'"$TMP"'/$0" && cd "'"$TMP"'/$0" &&
    env -i LD_LIBRARY_PATH="'"$LIBS"'" "'"$VP"'" -R 64 -f "'"$ROOT"'/$1" \
        --max-instr 30000000 > out.log 2>&1 || true'

python3 - "$ROOT/tests/reference/reference.csv" "$TMP" <<'PY'
import csv, re, sys
rows = list(csv.DictReader(open(sys.argv[1])))
bad = 0
print(f"{'program':22s} {'VP cycles':>11s} {'expected':>11s}  {'vs RTL':>8s}")
for r in rows:
    try:
        log = open(f"{sys.argv[2]}/{r['name']}/out.log").read()
    except OSError:
        log = ""
    v = [int(x) for x in re.findall(r"HALTER\] (?:0|1): (\d+)", log)[:2]]
    if len(v) < 2:
        print(f"{r['name']:22s} {'no result':>11s}"); bad += 1; continue
    cyc, ins = v
    ok = cyc == int(r["vp_cycles"]) and ins == int(r["vp_instret"])
    bad += not ok
    err = (cyc - int(r["rtl_cycles"])) * 100.0 / int(r["rtl_cycles"])
    print(f"{r['name']:22s} {cyc:11,d} {int(r['vp_cycles']):11,d}  {err:+7.2f}%  {'ok' if ok else 'CHANGED'}")
print(f"{len(rows) - bad} of {len(rows)} unchanged")
sys.exit(1 if bad else 0)
PY
