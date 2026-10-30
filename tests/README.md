# Tests

Every timing test runs the same ELF on the VP and on the CVA6 RTL co-sim.
For each program the build produces a `.hex` (VP: `RISCV_VP -R 64 -f <x>.hex`)
and a `.csv` (co-sim: `Vcva6 <x>.csv`) from one `.elf`.

| Directory | Contents | Build | Compiler |
|---|---|---|---|
| `micro/` | Directed microbenchmarks of paper Table III (`int-alu`, `int-nob`, `int-ind`, `div`, `ltest3`, `robust`, `fp-b`, `fp-s`), plus `ptr_chase`, `spmv_int`, `mem_contention` | `micro/build.sh` | riscv-none-elf GCC 13.1 |
| `fpmb/` | The paired timing probes of paper Section V (FP chains, integer same-register pairs, load-use distances, divider) | `fpmb/build.sh` | riscv64-linux-gnu GCC 7.5 |
| `dmiss/` | D-cache miss probes used to fit the miss fill tail | `dmiss/build.sh` | GCC 7.5 |
| `ldbr/` | Load-fed branch probes | `ldbr/build.sh` | GCC 7.5 |
| `coremark/`, `dhrystone/`, `median/`, `spmv/`, `mem/` | Whole programs and memory tests of paper Table IV (shared `coremark/crt0.S` and `link.ld`) | `build.sh` in each | GCC 7.5 |
| `full_system/` | Sources of the memory and cache stress tests (`ptr_chase`, `cache_sweep`, `locality`, `mem_contention`, ...) | see `micro/`, `mem/` | |
| `hex/` | Committed VP images of the benchmarks above | | |
| `riscv-tests/` | Submodule: official RISC-V ISA tests; source of `median`, `multiply` and SpMV | | |
| `C/`, `CPP/`, `FreeRTOS*/`, `asm/` | Functional examples inherited from RISC-V-TLM | | |

`micro/build.sh verify` and `fpmb/build.sh verify` rebuild every program and
check that the result is byte-identical to the committed image. Both pass
with the compilers above; a different GCC version produces different code and
therefore different cycle counts.

`micro/multiply_bare.c` (riscv-tests `multiply`, cited in paper Section VII-C)
is kept for reference only: its published binary cannot be rebuilt
byte-identically from this source.
