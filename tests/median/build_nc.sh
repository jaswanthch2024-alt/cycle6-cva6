#!/bin/bash
# median and median_sorted built without the C extension (all 32-bit instructions).
set -euo pipefail
cd "$(dirname "$0")"
for v in median_bare:median_nc median_sorted:median_sorted_nc; do
  src=${v%%:*}; out=${v##*:}
  riscv64-linux-gnu-gcc -march=rv64im -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static \
      -Wl,--build-id=none -T ../coremark/link.ld -I. -I../riscv-tests/benchmarks/median \
      ../coremark/crt0.S $src.c -o $out.elf
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
