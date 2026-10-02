#!/bin/bash
# Build the shared CoreMark ELF for the Cycle6 VP and the CVA6 RTL co-sim.
#   ./build.sh [ITERATIONS]          (run inside the riscv-vp-dev container)
# Outputs: coremark.elf, coremark.hex (VP), coremark.bin, coremark.csv (co-sim)
set -euo pipefail
cd "$(dirname "$0")"
ITER=${1:-10}
CC=riscv64-linux-gnu-gcc
CFLAGS="-march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static"
FLAGS_STR="\"$CFLAGS\""

$CC $CFLAGS -DITERATIONS=$ITER -DFLAGS_STR="$FLAGS_STR" \
    -Wl,--build-id=none -T link.ld \
    crt0.S core_portme.c core_main.c core_list_join.c core_matrix.c core_state.c core_util.c \
    -o coremark.elf
riscv64-linux-gnu-objcopy -O ihex   coremark.elf coremark.hex
riscv64-linux-gnu-objcopy -O binary coremark.elf coremark.bin

# Co-sim CSV: one 64-bit little-endian word per line, from 0x80000000.
python3 - <<'EOF'
import struct
d = open("coremark.bin", "rb").read()
d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("coremark.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0]))
        a += 8
EOF
echo "built ITERATIONS=$ITER"
riscv64-linux-gnu-size coremark.elf
