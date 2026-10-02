#!/bin/bash
# Build dmiss variants: name stride mode  (shared crt0/link.ld, one ELF for VP and co-sim)
set -euo pipefail
cd "$(dirname "$0")"
for v in "d16 16 0" "d8 8 0" "i16 16 1" "i8 8 1"; do
  set -- $v
  riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O1 -fno-builtin -ffreestanding -nostdlib -static \
      -Wl,--build-id=none -T ../coremark/link.ld -DSTRIDE=$2 -DMODE=$3 ../coremark/crt0.S dmiss.c -o dmiss_$1.elf
  riscv64-linux-gnu-objcopy -O ihex dmiss_$1.elf dmiss_$1.hex
  riscv64-linux-gnu-objcopy -O binary dmiss_$1.elf dmiss_$1.bin
  python3 - "$1" <<'PY'
import struct, sys
n = sys.argv[1]
d = open("dmiss_%s.bin" % n, "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("dmiss_%s.csv" % n, "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
done
ls dmiss_*.csv
