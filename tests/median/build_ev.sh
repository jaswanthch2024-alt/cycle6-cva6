#!/bin/bash
# median with the HPM events changed to if_empty, stall_issue, sb_full, loads,
# stores, integer instructions; compressed (median_ev) and without C (median_nc_ev).
set -euo pipefail
cd "$(dirname "$0")"
for v in "rv64imac:median_ev" "rv64im:median_nc_ev"; do
  arch=${v%%:*}; out=${v##*:}
  riscv64-linux-gnu-gcc -march=$arch -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static \
      -Wl,--build-id=none -T ../coremark/link.ld -I../riscv-tests/benchmarks/median \
      ../coremark/crt0.S median_ev.c -o $out.elf
  riscv64-linux-gnu-objcopy -O ihex $out.elf $out.hex
  riscv64-linux-gnu-objcopy -O binary $out.elf $out.bin
  python3 - "$out" <<'PY'
import struct, sys
o = sys.argv[1]
d = open(o + ".bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open(o + ".csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
done
