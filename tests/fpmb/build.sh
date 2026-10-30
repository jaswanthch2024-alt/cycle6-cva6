#!/bin/bash
# Paired timing probes (paper Section V): FP latency and same-register chains,
# integer dependency/same-register pairs, load-use distances, divider.
# Each probe is one ELF run on both the VP and the CVA6 co-sim.
#
# Toolchain: riscv64-linux-gnu-gcc 7.5 (riscv-vp-dev container). The
# committed .elf files were built with exactly these flags; another GCC
# version gives different code.
#
#   ./build.sh            rebuild every fpmb_*.c into .elf/.hex/.bin/.csv
#   ./build.sh verify     rebuild into a temporary dir and compare with the
#                         committed .elf images (must be byte-identical)
#
# gen_int.py and gen_pair.py generate some of the fpmb_*.c sources.
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-riscv64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-riscv64-linux-gnu-objcopy}
"$CC" --version | head -1
FLAGS="-march=rv64g -mabi=lp64d -O1 -mcmodel=medany -nostdlib -static \
  -Wl,--build-id=none -Wl,--entry=main -Wl,-Ttext=0x80000000"

csv() {  # bin -> co-sim CSV (one 64-bit little-endian word per line)
  python3 - "$1" "$2" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open(sys.argv[2], "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0]))
        a += 8
PY
}

if [ "${1:-}" = "verify" ]; then
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  ok=0; fail=0
  for src in fpmb_*.c; do
    n=${src%.c}
    # shellcheck disable=SC2086
    "$CC" $FLAGS "$src" -o "$tmp/$n.elf"
    "$OBJCOPY" -O binary "$tmp/$n.elf" "$tmp/a.bin"
    "$OBJCOPY" -O binary "$n.elf" "$tmp/b.bin"
    if cmp -s "$tmp/a.bin" "$tmp/b.bin"; then ok=$((ok+1)); else echo "DIFFERENT $n"; fail=1; fi
  done
  echo "identical: $ok"
  exit $fail
fi

for src in fpmb_*.c; do
  n=${src%.c}
  # shellcheck disable=SC2086
  "$CC" $FLAGS "$src" -o "$n.elf"
  "$OBJCOPY" -O ihex   "$n.elf" "$n.hex"
  "$OBJCOPY" -O binary "$n.elf" "$n.bin"
  csv "$n.bin" "$n.csv"
done
echo "built $(ls fpmb_*.c | wc -l) probes"
