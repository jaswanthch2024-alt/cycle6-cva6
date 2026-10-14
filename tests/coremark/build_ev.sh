#!/bin/bash
# CoreMark (compressed, 10 iterations) with the HPM events changed to
# if_empty, stall_issue, sb_full, loads, stores, integer instructions.
set -euo pipefail
cd "$(dirname "$0")"
CFLAGS="-march=rv64imac -mabi=lp64 -mcmodel=medany -O2 -fno-builtin -ffreestanding -nostdlib -static"
riscv64-linux-gnu-gcc $CFLAGS -DITERATIONS=10 -DFLAGS_STR="\"$CFLAGS\"" \
    -Wl,--build-id=none -T link.ld \
    crt0.S core_portme_ev.c core_main.c core_list_join.c core_matrix.c core_state.c core_util.c \
    -o coremark_ev.elf
riscv64-linux-gnu-objcopy -O ihex coremark_ev.elf coremark_ev.hex
riscv64-linux-gnu-objcopy -O binary coremark_ev.elf coremark_ev.bin
python3 - <<'PY'
import struct
d = open("coremark_ev.bin", "rb").read(); d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open("coremark_ev.csv", "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0])); a += 8
PY
