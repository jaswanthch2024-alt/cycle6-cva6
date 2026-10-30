#!/bin/bash
# Directed microbenchmarks (paper Table III) and three memory stress tests,
# built from one ELF for both the VP and the CVA6 co-sim.
#
# Toolchain: riscv-none-elf GCC 13.1.0 (/opt/riscv/bin in the cva6-dev
# container; override with RV=...). This is the compiler the published
# binaries were built with; another GCC version gives different code.
#
#   ./build.sh            build everything into out/
#   ./build.sh verify     build, then compare with the committed tests/hex/
#                         images (must be byte-identical)
#
# Per program: out/<name>.elf, .hex (Intel HEX, VP: RISCV_VP -f),
# .bin, .csv (co-sim: Vcva6 <name>.csv).
set -euo pipefail
cd "$(dirname "$0")"
RV=${RV:-/opt/riscv/bin}
CC=$RV/riscv-none-elf-gcc
AS=$RV/riscv-none-elf-as
LD=$RV/riscv-none-elf-ld
OBJCOPY=$RV/riscv-none-elf-objcopy
"$CC" --version | head -1

INT_FLAGS="-fPIC -march=rv64g -mabi=lp64f -O1 -fno-builtin"
FP_FLAGS="-march=rv64g -mabi=lp64d -mcmodel=medany -O1 -fno-builtin"

# name : source : flags
PROGRAMS=(
  "int_alu_bench:int_alu_bench.c:$INT_FLAGS"
  "int_nobranch_bench:int_nobranch_bench.c:$INT_FLAGS"
  "int_indep_bench:int_indep_bench.c:$INT_FLAGS"
  "div_stress_bare:div_stress_bare.c:$INT_FLAGS"
  "long_test3_bare:long_test3_bare.c:$INT_FLAGS"
  "robust_stress_bare:robust_stress_bare.c:$INT_FLAGS"
  "fp_bench:fp_bench.c:$FP_FLAGS"
  "fp_stress_bare:fp_stress_bare.c:$FP_FLAGS"
  "ptr_chase_bench:../full_system/ptr_chase_bench.c:$INT_FLAGS"
  "spmv_int_bench:../full_system/spmv_int_bench.c:$INT_FLAGS -I../full_system"
  "mem_contention_bench:../full_system/mem_contention_bench.c:$INT_FLAGS"
)

mkdir -p out
for entry in "${PROGRAMS[@]}"; do
  IFS=: read -r name src flags <<< "$entry"
  # shellcheck disable=SC2086
  "$CC" $flags -S "$src" -o "out/$name.S"
  "$AS" -march=rv64g "out/$name.S" -o "out/$name.o"
  "$LD" -T link.ld -m elf64lriscv -o "out/$name.elf" "out/$name.o"
  "$OBJCOPY" -O ihex   "out/$name.elf" "out/$name.hex"
  "$OBJCOPY" -O binary "out/$name.elf" "out/$name.bin"
  python3 - "out/$name.bin" "out/$name.csv" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
d += b"\0" * ((-len(d)) % 8) + b"\0" * 24
with open(sys.argv[2], "w") as f:
    a = 0x80000000
    for i in range(0, len(d), 8):
        f.write("0x%X,0x%016X\n" % (a, struct.unpack("<Q", d[i:i+8])[0]))
        a += 8
PY
  rm -f "out/$name.S" "out/$name.o"
  echo "built $name"
done

# multiply_bare.c (riscv-tests multiply kernel, cited in Section VII-C) is kept
# for reference only: its published binary cannot be rebuilt byte-identically
# from this source, so it is not part of PROGRAMS.

if [ "${1:-}" = "verify" ]; then
  fail=0
  for entry in "${PROGRAMS[@]}"; do
    name=${entry%%:*}
    "$OBJCOPY" -I ihex -O binary "../hex/$name.hex" "out/$name.ref.bin"
    if cmp -s "out/$name.bin" "out/$name.ref.bin"; then
      echo "identical  $name"
    else
      echo "DIFFERENT  $name"; fail=1
    fi
    rm -f "out/$name.ref.bin"
  done
  exit $fail
fi
