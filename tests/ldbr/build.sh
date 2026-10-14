#!/bin/bash
# Build the four load-fed-branch probes (VP .hex and RTL .csv from one ELF).
set -euo pipefail
cd "$(dirname "$0")"
for v in A B C D; do
  riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static \
      -Wl,--build-id=none -T ../coremark/link.ld ../coremark/crt0.S ldbr_$v.c -o ldbr_$v.elf
  riscv64-linux-gnu-objcopy -O ihex ldbr_$v.elf ldbr_$v.hex
  riscv64-linux-gnu-objcopy -O binary ldbr_$v.elf ldbr_$v.bin
  python3 - "$v" <<'PY'
import struct, sys
v = sys.argv[1]
d = open(f"ldbr_{v}.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open(f"ldbr_{v}.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
done
