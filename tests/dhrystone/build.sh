#!/bin/bash
# Dhrystone for VP + CVA6 co-sim, using the shared CoreMark startup (crt0.S)
# and linker script so the entry is at 0x80000000 (the co-sim bootrom jumps
# there; the original samples/ build had main at 0x8000033c and only ran on
# the VP). Outputs dhrystone.{elf,hex,csv}.
set -euo pipefail
cd "$(dirname "$0")"
riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding \
    -nostdlib -static -Wl,--build-id=none -T ../coremark/link.ld \
    ../coremark/crt0.S dhrystone_bare.c -o dhrystone.elf
riscv64-linux-gnu-objcopy -O ihex   dhrystone.elf dhrystone.hex
riscv64-linux-gnu-objcopy -O binary dhrystone.elf dhrystone.bin
python3 - <<'PY'
import struct
d = open("dhrystone.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("dhrystone.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
riscv64-linux-gnu-readelf -h dhrystone.elf | grep Entry
