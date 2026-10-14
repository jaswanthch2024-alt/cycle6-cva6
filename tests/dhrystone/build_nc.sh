#!/bin/bash
# Dhrystone without the C extension (all 32-bit instructions).
# and linker script so the entry is at 0x80000000 (the co-sim bootrom jumps
# there; the original samples/ build had main at 0x8000033c and only ran on
# the VP). Outputs dhrystone.{elf,hex,csv}.
set -euo pipefail
cd "$(dirname "$0")"
riscv64-linux-gnu-gcc -march=rv64im -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding \
    -nostdlib -static -Wl,--build-id=none -T ../coremark/link.ld \
    ../coremark/crt0.S dhrystone_bare.c -o dhrystone_nc.elf
riscv64-linux-gnu-objcopy -O ihex   dhrystone_nc.elf dhrystone_nc.hex
riscv64-linux-gnu-objcopy -O binary dhrystone_nc.elf dhrystone_nc.bin
python3 - <<'PY'
import struct
d = open("dhrystone_nc.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("dhrystone_nc.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
riscv64-linux-gnu-readelf -h dhrystone_nc.elf | grep Entry
