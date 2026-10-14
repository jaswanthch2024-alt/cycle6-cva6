#!/bin/bash
# median with sorted input: same code and values as median_bare.c, but almost no mispredicts.
set -euo pipefail
cd "$(dirname "$0")"
riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static \
    -Wl,--build-id=none -T ../coremark/link.ld -I. -I../riscv-tests/benchmarks/median \
    ../coremark/crt0.S median_sorted.c -o median_sorted.elf
riscv64-linux-gnu-objcopy -O ihex median_sorted.elf median_sorted.hex
riscv64-linux-gnu-objcopy -O binary median_sorted.elf median_sorted.bin
python3 - <<'PY'
import struct
d = open("median_sorted.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("median_sorted.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
