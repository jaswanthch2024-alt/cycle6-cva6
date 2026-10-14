#!/bin/bash
# Memory-locality benchmarks for VP + CVA6 co-sim from one ELF (shared crt0.S/link.ld).
set -euo pipefail
cd "$(dirname "$0")"
for n in locality_bench cache_sweep_bench; do
  riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O1 -fno-builtin -ffreestanding -nostdlib -static \
      -Wl,--build-id=none -T ../coremark/link.ld ../coremark/crt0.S $n.c -o $n.elf
  riscv64-linux-gnu-objcopy -O ihex $n.elf $n.hex
  riscv64-linux-gnu-objcopy -O binary $n.elf $n.bin
  python3 - "$n" <<'PY'
import struct, sys
n = sys.argv[1]
d = open(n + ".bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open(n + ".csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
done
