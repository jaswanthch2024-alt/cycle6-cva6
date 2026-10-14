#!/bin/bash
# median for VP + CVA6 co-sim from one ELF (shared crt0.S/link.ld keeps data off 0x80001000).
set -euo pipefail
cd "$(dirname "$0")"
riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static \
    -Wl,--build-id=none -T ../coremark/link.ld -I../riscv-tests/benchmarks/median \
    ../coremark/crt0.S median_bare.c -o median.elf
riscv64-linux-gnu-objcopy -O ihex median.elf median.hex
riscv64-linux-gnu-objcopy -O binary median.elf median.bin
python3 - <<'PY'
import struct
d = open("median.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("median.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
riscv64-linux-gnu-size median.elf
