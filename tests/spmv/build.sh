#!/bin/bash
# Double-precision SpMV for VP + CVA6 co-sim from one ELF (shared crt0.S enables the FPU).
set -euo pipefail
cd "$(dirname "$0")"
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -O1 -fno-builtin -ffreestanding \
    -nostdlib -static -Wl,--build-id=none -T ../coremark/link.ld \
    ../coremark/crt0.S spmv_bare.c -o spmv.elf
riscv64-linux-gnu-objcopy -O ihex   spmv.elf spmv.hex
riscv64-linux-gnu-objcopy -O binary spmv.elf spmv.bin
python3 - <<'PY'
import struct
d = open("spmv.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("spmv.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
riscv64-linux-gnu-readelf -h spmv.elf | grep Entry; riscv64-linux-gnu-size spmv.elf
