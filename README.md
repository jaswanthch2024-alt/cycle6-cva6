# Cycle6: a cycle-accurate SystemC/TLM-2.0 virtual prototype of the CVA6 core

A cycle-accurate, CVA6-aligned RISC-V 64-bit virtual platform implemented in SystemC/TLM-2.0.
Boots **Linux 6.1.0** to a BusyBox user-space shell on the 6-stage pipeline model, and includes
an optional real-AXI memory-contention model validated against CVA6 RTL co-simulation.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![SystemC](https://img.shields.io/badge/SystemC-3.0.2-green.svg)](https://www.accellera.org/downloads/standards/systemc)

**Repository:** [github.com/jaswanthch2024-alt/cycle6-cva6](https://github.com/jaswanthch2024-alt/cycle6-cva6)

---

## What This Is

RISCV-VP is a pre-silicon software validation platform. It models the microarchitecture of the
**CVA6 (Ariane)** open-source application-class RISC-V processor at the cycle level, allowing
software engineers to run real workloads (including a full Linux kernel) and obtain detailed
pipeline performance statistics before silicon is available. Every timing constant in the
pipeline — cache geometry, replacement policy, predictor sizes, miss latencies — is calibrated
against direct CVA6 RTL co-simulation, not assumed; see [`docs/BUGS.md`](docs/BUGS.md) for the
full calibration history.

SystemC and spdlog are git submodules (`systemc/`, `spdlog/`), so clone with
`--recursive`. Nothing else is needed beyond a C++17 compiler and CMake.

---

## Quick Start — Build & Test on Your Own Desktop

```bash
git clone --recursive https://github.com/jaswanthch2024-alt/cycle6-cva6.git cycle6-cva6
cd cycle6-cva6
# (already cloned without --recursive? run: git submodule update --init systemc spdlog)

# Build the RTL-validated model (CYCLE6_AXI is also the default TIMING_MODEL)
mkdir -p build_axi && cd build_axi
cmake .. -DTIMING_MODEL=CYCLE6_AXI -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

# Run a bare-metal benchmark (seconds): Table III int-alu, expect 2,400,130 cycles
export LD_LIBRARY_PATH=build_axi:build_axi/systemc/src:build_axi/spdlog
./build_axi/RISCV_VP -R 64 -f tests/hex/int_alu_bench.hex --max-instr 30000000

# Boot Linux to a shell (about 7 minutes with this build)
./build_axi/RISCV_VP -R 64 \
  --bios  boot/fw_jump.bin \
  --dtb   boot/vp.dtb \
  --kernel boot/Image \
  --initrd boot/initramfs.cpio.gz
```

Expected microbenchmark output ends with a `printStats()` block (`Cycles:`, `IPC:`, cache miss
rates, etc.). Expected Linux boot output reaches:

```
...
[    0.000000] Linux version 6.1.0 ...
Run /init as init process
RISCV-VP: Starting interactive shell...
~ #
```

### Dependencies

| Requirement | Notes |
|---|---|
| CMake ≥ 3.10 | |
| C++17 compiler | Verified with GCC 13.3 and GCC 15.2; both give identical cycle counts |
| Nothing else | SystemC and spdlog come from the submodules |

By default (`-DUSE_LOCAL_SYSTEMC=ON`) CMake builds the vendored `systemc/` as part of the same
`make`, even if another SystemC is installed, so every machine uses the same version. The
`CYCLE6_AXI*` builds need it: MatchLib uses SystemC kernel headers that installed packages
usually leave out. `-DUSE_LOCAL_SYSTEMC=OFF` uses an installed SystemC instead.

Any `VP_*` environment variable that is set changes the model, and the VP lists every one it
sees at start-up (`env : ...`). Run with none set to get the configuration in the paper.

To rebuild the benchmarks themselves, see [`tests/README.md`](tests/README.md): the published
binaries come from GCC 7.5 and GCC 13.1, and the build scripts check that a rebuild is
byte-identical.

### Troubleshooting: `No CMAKE_C_COMPILER could be found`

On RHEL/AlmaLinux/CentOS-family systems (including the `almalinux`-based dev containers this
project has been built and tested in), there is often **no default `gcc`/`g++` on `PATH`** —
only an opt-in GCC Toolset (e.g. `gcc-toolset-13`) that needs activating first:

```bash
export PATH=/opt/rh/gcc-toolset-13/root/usr/bin:$PATH   # adjust version to what's installed
cmake .. -DTIMING_MODEL=CYCLE6_AXI -DCMAKE_BUILD_TYPE=Release
```

(Verified: a genuinely fresh clone in this environment fails with "No CMAKE_C_COMPILER could be
found" until this `PATH` export is done — Ubuntu/Debian/Fedora desktops with a default system
`gcc` package installed do not need this step.)

---

## Simulation Timing Tiers

| Model | `TIMING_MODEL` | Speed | Use Case |
|-------|------|-------|----------|
| LT (Loosely-Timed) | `LT` (default) | ~100 MIPS | Fast functional, software debug |
| AT (Approximately-Timed) | `AT` | ~50 MIPS | Bus/interconnect latency analysis |
| 2-Stage Cycle | `CYCLE` | ~10 MIPS | Basic hardware timing |
| **6-Stage Cycle (Cycle6)** | `CYCLE6` | ~2–5 MIPS | **Full micro-arch simulation — the model this README describes** |
| **6-Stage Cycle + real AXI contention** | `CYCLE6_AXI` | slower (Connections/matchlib) | Microbenchmark-only; models real I$/D$ AXI port contention, see below |

---

## System Architecture

```
┌──────────────────────────────────────────────────────────┐
│                          VPTop                           │
│                                                          │
│  ┌─────────────────┐     ┌──────────┐     ┌────────────┐ │
│  │ CPURV64P6_Cycle │<===>│  BusCtrl │<===>│ Memory     │ │
│  │ (6-stage CVA6)  │     │          │     │ (512 MB)   │ │
│  └─────────────────┘     │          │<===>│ CLINT      │ │
│   ^  ^  ^                │          │     │ PLIC       │ │
│   │  │  │ IRQ lines      │          │<===>│ UART       │ │
│  timer msip ext          └──────────┘     └────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### Physical Address Map

| Address | Peripheral |
|---------|-----------|
| `0x0200_0000` | CLINT (timer + MSIP) |
| `0x0C00_0000` | PLIC (external interrupts) |
| `0x1000_0000` | UART 16550 (serial console) |
| `0x8000_0000` | DRAM — 512 MB (OpenSBI + DTB + kernel + initrd) |
| `0x8000_1000` | HTIF tohost (riscv-tests pass/fail) |

### Boot Chain

```
fw_jump.bin  →  Linux 6.1.0  →  BusyBox initramfs  →  ~ #
 (OpenSBI)      (S-mode)          (U-mode)
```

---

## Cycle6 Pipeline — Microarchitecture Detail

### Pipeline Stages

```
PCGen ──► Fetch ──► Decode ──► Issue ──► EX/MEM ──► Commit
  1          2         3          4          5           6
```

| Stage | Responsibility |
|-------|---------------|
| PCGen | Next-PC: sequential / branch prediction / exception redirect |
| Fetch | Instruction memory read, I-cache lookup, ITLB translation, 4-entry decoupling FIFO |
| Decode | Instruction decode, immediate extraction, register index identification |
| Issue | Operand read + forwarding, scoreboard allocation, hazard check |
| EX/MEM | ALU, branch resolution, load/store (D-cache + store buffer), MUL/DIV/FPU |
| Commit | In-order retirement, architectural register writeback, store drain to memory |

### Hazard Handling

| Hazard | Mechanism |
|--------|-----------|
| RAW (data) | Scoreboard `rd_clobber` + same-cycle forwarding from completed entries |
| Load-use (D$ hit) | `load_hit_pipe` (pipelined, one load per cycle) delivers the result 2 cycles later: CVA6's 3-cycle hit latency, measured on six load-use microbenchmarks to 0.05% (`docs/BUGS.md` §B11) |
| Load-use (D$ miss) | AXI refill (arbiter + slave), then a fill tail: result usable 4 cycles after the refill, miss path occupied for 3 of them (`dmiss_extra`/`dmiss_block`, `docs/BUGS.md` §B15). Non-AXI build: `dcache_miss_penalty` (10) |
| Load→address dependency (gather / pointer-chase) | A load result feeding a subsequent load/store address costs an extra `load_addr_penalty` (2) cycles — matches CVA6's route-to-address-generation latency, verified to 0.006% on an isolated pointer-chase benchmark; see `docs/BUGS.md` §B9 |
| Store-buffer partial overlap | Tri-state forward result: HIT / PARTIAL_OVERLAP / MISS; stalls on overlap |
| Structural (MUL/DIV/FPU) | Issue stalls new instruction while FU is occupied |
| Same destination register (WAW) | An integer ALU/MUL op writing the same register as the op just before it waits one cycle (CVA6 issue waits for the earlier writer to commit). This replaces the old "shift bubble", which was a misattribution — see `docs/BUGS.md` §B12 |
| Branch misprediction / any pipeline flush | Full pipeline flush + redirect from EX stage, plus a 1-cycle frontend-redirect bubble on branch/jump mispredict flushes only. Closes `robust_stress`'s branch-mispredict IPC error from +2.8% to ~0.0% (`docs/BUGS.md` §B6) |
| CSR RAW | Serialisation stall in Issue until all older instructions commit |
| SYSTEM (ECALL/EBREAK) | Issue stalls until all multi-cycle FUs are idle |

### Branch Predictor

RTL-matched configuration (`docs/BUGS.md`, commit that fixed the earlier oversized default):

Prediction follows CVA6's frontend rules (`cva6/core/frontend/`, `docs/BUGS.md` §B13). The fetched
instruction is scanned in Fetch, as CVA6's `instr_scan` does:

| Structure / case | Configuration | Rule |
|-----------|--------------|-----------|
| `jal` | — | Always predicted taken, target from the immediate (no BTB) |
| BHT | **128 entries**, indexed PC[7:1] | Conditional branches: valid bit + 2-bit counter (reset 00); invalid entry → backward taken, forward not taken; target from the immediate |
| RAS | **2 entries** | Push on call, pop on return at prediction time; never flushed |
| BTB | **32 entries**, tagless, PC[5:1] | Indirect `jalr` (not returns); written only by a mispredicted `jalr` |

`VP_BP=legacy` restores the original VP predictor (BTB-gated, RAS cleared on mispredict), which
mispredicted 58% more than the RTL on CoreMark.

Linux 6.1.0 boots to the BusyBox shell on the frozen default configuration (AXI build):
**136,596,724 instructions / 222,179,066 cycles, IPC 0.615**, 4.27M mispredicts (76.5% predict
rate), 426 s wall-clock (0.32 MIPS, measured with other simulations running on the host).
With the original VP predictor the same boot mispredicted twice as often (8.31M, 56.3%,
IPC 0.586); the paper's 0.597 was measured on that older model. There is no RTL reference for
Linux boot (the co-sim testbench cannot boot Linux), so boot IPC shows the model's behaviour, not
a validated number. A boot hang specific to the CVA6 rules (`c.ebreak` scanned like `c.jalr`) is
fixed, see `docs/BUGS.md` §B14.

### Memory Hierarchy

| Level | Size | Config | Replacement | Miss Penalty |
|-------|------|--------|-------------|-------------|
| I-cache | 16 KB | 4-way, **16 B lines**, 256 sets | **LFSR pseudo-random** | **7 cycles** |
| D-cache | 32 KB | 8-way, **16 B lines**, 256 sets | **LFSR pseudo-random** | 10 cycles |
| ITLB | **16 entries** | Fully associative | — | 2 cycles/PTW level |
| DTLB | **16 entries** | Fully associative | — | 2 cycles/PTW level |
| DRAM | 512 MB | Flat array | — | — (no L2 modelled) |

Cache geometry, replacement policy, and I$ miss latency are all directly matched/re-derived
against CVA6 RTL — see `docs/BUGS.md` §B8 (LRU→LFSR replacement fix, was over-missing by ~22%)
and §B10 (I$ miss penalty re-derived from 10→7 cycles via an isolated single-cache benchmark,
matched to 0.046% error). **D$ miss penalty (10 cycles) has not yet been independently
re-derived the same way — a known open item, likely similarly over-stated.**

`Cache.h` implements six replacement policies — **LFSR** (default, matches CVA6 silicon), **LRU**,
**MRU**, **PLRU**, **NRU**, and **LFU** — selectable independently per cache at runtime via
`VP_ICACHE_POLICY`/`VP_DCACHE_POLICY` (see the AXI section's env-var table below), for
comparison-mode experiments only. Tested against three structurally different workload shapes
(pure cyclic scan, static hot/cold locality, and a phase-shifting hot region that moves over
time): **no policy is universally best.** MRU/LFU win by a wide margin on the first two workload
types but **catastrophically lose** on the phase-shifting one (LFU keeps protecting stale
historical "hotness" instead of the new phase; MRU evicts its own just-loaded data) — hit rate
drops from ~94% to ~57-65% and cycles balloon accordingly. LFSR is never the best on any single
workload, but it's never the worst either, which is the actual engineering rationale for why real
CVA6 silicon uses it over a deterministic policy: no structural blind spot for *any* access
pattern to exploit. See `docs/BUGS.md` §B8.

### Functional Unit Latencies

| Unit | Latency | Instructions |
|------|---------|-------------|
| ALU | 1 cycle | All integer arithmetic/logic |
| Load (D$ hit) | 1 cycle + 1 stall | LB / LH / LW / LD — RTL doc states 3 cycles; open investigation, see `docs/BUGS.md` §B11 |
| Load (D$ miss) | 10 cycles | LB / LH / LW / LD |
| Load→address dependency | +2 cycles (on top of the above) | Only when the loaded value feeds a subsequent load/store address (gather patterns) |
| Store | 0 (buffered) | SB / SH / SW / SD — verified against RTL to be genuinely free (CVA6's write-through buffer absorbs stores), see `docs/BUGS.md` §B7 |
| MUL | 2 cycles | MUL / MULH / MULHU / MULHSU / MULW |
| DIV | Operand-dependent, ~1–70 cyc | DIV / DIVU / REM / REMU + W-variants; early-terminating serial divider (latency ∝ significant quotient bits, matches CVA6 to within ~0.6%) |
| FPU | 2–14 cycles | F / D extension operations |
| CSR | 1 cycle (at commit) | CSRRW / CSRRS / CSRRC / CSRRWI / … |

### Store Buffer

Split speculative/committed design (matching CVA6):

- **4 speculative slots** — allocated at Issue, visible to forwarding logic
- **4 committed slots** — drained to memory at Commit in-order
- **Forwarding**: newest-first scan with tri-state result (HIT / PARTIAL_OVERLAP / MISS)
- Partial overlap stalls the load until the overlapping store drains
- Drain cost is zero cycles, independently confirmed against RTL (a store-flood benchmark shows
  CVA6's own write buffer makes stores effectively free too — see `docs/BUGS.md` §B7)

### D$ Miss Buffering

The D-cache miss unit supports **2 concurrent outstanding misses** (matching CVA6's
`NrLoadBufEntries=2`) in the default `CYCLE6` build — a second independent load can have its
miss latency overlap the first instead of queuing behind it. This is disabled (falls back to 1
slot) under the `CYCLE6_AXI` build, where the matchlib arbiter models a single D$ AXI master by
design.

### Privilege Architecture

| Feature | Status |
|---------|--------|
| M / S / U privilege modes | Implemented |
| mstatus / sstatus (UXL/SXL = 2 at reset) | Implemented |
| mie / mip / sie / sip | Implemented |
| MTVEC / STVEC vectored trap delivery | Implemented |
| MEPC / SEPC (correct value after MRET/SRET) | Implemented |
| ECALL / EBREAK / MRET / SRET / WFI | Implemented |
| PMP (4 regions) | Implemented |
| Hardware performance counters (mcycle, minstret) | Implemented |

### Virtual Memory — sv39

| Feature | Detail |
|---------|--------|
| Address translation mode | sv39 (39-bit VA, 3-level page table) |
| ITLB | 16-entry fully associative, global-bit aware |
| DTLB | 16-entry fully associative |
| Page table walker | Hardware 3-level walk, 2 cycles per PTE read |
| sfence.vma | Full TLB flush including global entries (rs1=x0, rs2=x0) |
| satp modes accepted | 0 (bare), 8 (sv39) — others silently ignored per spec |

---

## ISA Coverage

| Extension | Description | Status |
|-----------|-------------|--------|
| RV64I | Base integer | Complete |
| RV64M | Multiply / divide | Complete |
| RV64A | Atomics (LR/SC, AMOs) | Complete |
| RV64C | Compressed 16-bit instructions | Complete |
| RV64F | Single-precision floating point | Complete |
| RV64D | Double-precision floating point | Complete |
| Zicsr | CSR instructions | Complete |
| Zifencei | Instruction-fetch fence | Complete |

---

## AXI Contention Model (`CYCLE6_AXI` / `CYCLE6_AXI128`) — optional, microbenchmark-only

A separate, opt-in build mode that models real AXI-level contention between the I$ and D$ AXI
manager ports using an actual **NVIDIA matchlib `AxiArbiter`** (not a hand-rolled stand-in) —
vendored from the public [Stuart-Swan Matchlib-Examples-Kit](https://github.com/Stuart-Swan/Matchlib-Examples-Kit-For-Accellera-Synthesis-WG)
into `third_party/matchlib_kit/` (13 MB, trimmed, license files retained), which builds against
this project's own SystemC **3.0.2** — no Catapult HLS license required.

### What it models

- I$ and D$ modelled as **two independent AXI masters** contending for one arbitrated memory
  port, matching CVA6's real single AXI manager port.
- **Genuine multi-beat AXI INCR bursts** per cache-line refill, beat count derived from **each
  cache's own configured line width** independently (`Cache<>::LineBytes ÷ (AxiDataWidth/8)`) —
  at the default 64-bit AXI width and 16-byte line, that's 2 beats per line, matching CVA6's own
  RTL formula (`AxiRdBlenIcache/Dcache = LINE_WIDTH/AxiDataWidth - 1 = 128/64 - 1 = 1`, i.e. 2
  beats). I$ and D$ each have their own independent `AxiRefillMaster`, so this scales correctly
  even when the two caches are configured with different line widths — an earlier version of this
  formula hardcoded a 16-byte assumption for both caches regardless of actual configured line
  width, which silently undercounted refill cost whenever a line-width sweep moved either cache
  away from 16 bytes (now fixed).
- The AXI channel carries the CPU's **real** instruction/data values (not a synthetic
  placeholder) — verified by an always-on runtime check comparing every AXI-channel value
  against the value fetched via the normal `mem_intf` path; this ran clean (zero mismatches)
  across microbenchmarks and a full Linux boot to the interactive shell, including thousands
  of DTLB-miss-combined page-table-walk cases. Note: this makes the *timing* channel
  functionally data-correct in simulation; it does not add an external AXI port anything
  outside the VP could bind to (see caveats below).
- The default `CYCLE6` build's core pipeline logic is **completely untouched** by this mode —
  verified bit-exact when `CYCLE6_AXI`/`CYCLE6_AXI128` is not selected.

### Runtime comparison switches (single build, env-var toggle — no rebuild needed)

These default to the RTL-accurate value; overriding them is for side-by-side
what-if experiments only, not an RTL-accuracy claim:

| Env var | Default | What it does |
|---|---|---|
| `VP_CACHE_POLICY=X` | LFSR (matches CVA6 silicon) | Sets **both** L1 caches to policy `X` (LFSR/LRU/MRU/PLRU/NRU/LFU) for comparison. See the Memory Hierarchy section above for the finding: no policy is universally best — MRU/LFU win big on static workloads, lose big on phase-shifting ones. |
| `VP_ICACHE_POLICY=X` / `VP_DCACHE_POLICY=X` | (falls back to `VP_CACHE_POLICY`, else LFSR) | Same six policies, set **independently** per cache — overrides `VP_CACHE_POLICY` for just that one cache, so I$ and D$ policy can be swept separately without confounding the other cache's behavior. |
| `VP_AXI_BEATS=N` | Per-cache, derived from each cache's own line width (2 at the 64-bit/16-byte default) | Overrides the INCR burst length per refill for **both** caches uniformly. Lower = fewer beats = cheaper misses, same miss *count* — isolates the cycle-cost of AXI bus width without a separate build. |
| `VP_BP=legacy` | CVA6 frontend rules | Original VP predictor (comparison only) |
| `VP_LOAD_LAT=N` (1–4) | 2 | D$-hit load-use stall cycles (1 = old model) |
| `VP_WAW_BUBBLE=0/1/2` | 1 | Same-register stall: 0 = old shift-bubble rule, 2 = faithful commit wait (§B12) |
| `VP_DMISS_EXTRA=N` / `VP_DMISS_BLOCK=N` | 5 / 3 | D$ miss fill tail and how much of it blocks the next miss (0 = old model) |
| `VP_FPU_WAW=0`, `VP_FPU_LEGACY_BLOCK=1` | on / off | FP same-register commit wait; old "any FP op waits for div/sqrt" rule (§B16) |
| `VP_WATCHDOG=N`, `VP_MP_TRACE=file` | off | Diagnostics: dump pipeline state if nothing retires for N cycles; log every mispredict PC |
| `VP_DCACHE_SLOTS=N` (max 8) | 1 with AXI / 2 without | Number of D$ misses allowed outstanding at once (non-blocking D$ experiment, modelling what a wider MSHR like CVA6's HPDcache option would allow). Finding: raising this above the default had **no effect** on tested benchmarks — this pipeline's single-issue front end never generates more than ~2 concurrent outstanding misses regardless of slot capacity, so slot count isn't the bottleneck for non-blocking-cache-style concurrency here. |

### `CYCLE6_AXI128` / `CYCLE6_AXI256` / `CYCLE6_AXI512` / `CYCLE6_AXI1024` — wider AXI data widths (separate builds, not a runtime flag)

Unlike the switches above, AXI data width is a **compile-time build variant**
(`-DTIMING_MODEL=CYCLE6_AXI128`, `...AXI256`, `...AXI512`, `...AXI1024`), not an env var —
matchlib's AXI types are C++ templates on the data-width config, so a wider bus is a different
compiled binary, not a flag on the existing one. No CVA6 target ships anything wider than 64-bit
AXI (`AxiDataWidth=64` in every `cva6_config_pkg.sv`), so all four wider variants are genuinely
new, uncalibrated-against-RTL comparison points — useful for isolating "what does a wider bus buy
you," not for an RTL-accuracy claim.

| | 64-bit AXI (`CYCLE6_AXI`) | 128-bit | 256-bit | 512-bit | 1024-bit |
|---|---:|---:|---:|---:|---:|
| Beats per 16 B line refill | 2 | 1 | 1 | 1 | 1 |
| `mem_contention_bench` cycles | 4,266,611 | 3,836,662 | 3,836,662 | 3,836,662 | 3,836,662 |
| Miss count (unchanged at every width) | 332,743 I$ / 166,923 D$ | same | same | same | same |

**A real floor, verified directly, not just predicted from the formula:** a 16-byte line already
fits inside a single beat once the bus reaches 128-bit, so 128/256/512/1024-bit are **identical**
— widening further buys nothing for a single-cache-line refill, because you cannot transfer less
than one whole beat no matter how much bus capacity goes unused. This is not a limitation of the
model; it's the real physical floor of the AXI protocol, confirmed by a second test at a
deliberately larger 256-byte D$ line (which never hits the floor at any tested width): cycles
there scale cleanly and continuously across all five widths (37 → 21 → 13 → 9 → 7 stall
cycles/miss, tracking the beat count exactly: 32 → 16 → 8 → 4 → 2 beats). See
`tools/axi_burst_stress/` for the same floor-vs-no-floor contrast on pure bulk AXI transfers
(512B/4KB/64KB blocks, which stay bigger than one beat even at 1024-bit, so throughput keeps
improving ~5x across the full width sweep with no floor at all).

Same miss count at every width — any cycle reduction comes entirely from each existing miss
costing less to service (fewer AXI beats), never from catching more hits.

#### 1024-bit AXI + 1024-byte cache lines (combined, directional experiment)

`CYCLE6_AXI1024` build with both L1 line widths changed from 16 B to 1024 B
(`Cache<256, 4, 1024>` / `Cache<256, 8, 1024>` in `inc/CPU_P64_6_Cycle.h`; sets and ways held
fixed, so capacity grows to **1 MB I$ / 2 MB D$**). Not a shipped configuration — the header was
edited only for this run and reverted afterwards.

| Workload | Config | Instructions | Cycles | IPC | I$ misses | D$ misses |
|---|---|---:|---:|---:|---:|---:|
| `mem_contention_bench` | 1024-bit AXI, 16 B line | — | 3,836,662 | ~0.43 | 332,743 | 166,923 |
| `mem_contention_bench` | 1024-bit AXI, 1024 B line | 1,638,480 | 2,252,073 | 0.728 | 33 | 66 |
| Linux boot | 64-bit AXI, 16 B line (default) | 139,613,943 | 238,118,336 | 0.586 | 4,320,407 | 1,290,826 |
| Linux boot | 1024-bit AXI, 1024 B line | 138,469,695 | 209,158,015 | 0.662 | 5,818 | 23,730 |

- `mem_contention_bench` (64 KB region) fits entirely in the inflated caches, so it only shows
  "everything hits". Linux boot is large enough to keep missing even against 1 MB / 2 MB caches,
  which is why it was used as the real-workload check.
- Linux boot: −12% cycles, IPC 0.586 → 0.662. Mispredicts are unchanged (8.30M vs 8.31M), so the
  gain is entirely from memory stalls.
- **This does not isolate bus width.** Line size, cache capacity (16/32 KB → 1/2 MB) and AXI width
  all changed together, and most of the miss reduction comes from the capacity increase. Isolating
  the bus-width contribution would need a capacity-matched run (for example 1024 B lines with the
  set count cut to keep 16 KB / 32 KB), which has not been done.
- Uncalibrated: no CVA6 target uses wider than 64-bit AXI or lines larger than 16 B, and CVA6 RTL
  cannot boot Linux, so there is no RTL reference for any row here.

### What it deliberately does *not* model

- **Store-drain contention** — tested with a dedicated store-flood benchmark and found
  unnecessary: CVA6's write-through buffer makes stores effectively free (RTL shows ~0 D$
  misses under a store flood), matching the VP's existing free-store model. Adding contention
  here would move the VP *away* from RTL, not toward it.
- **DMA** — the co-sim reference this project validates against has no DMA peripheral and the
  CVA6 core itself has no DMA engine, so there is no RTL ground truth to validate a DMA AXI
  master against.
- **An external AXI port.** All of the above (arbiter, masters, slave) is instantiated and
  wired entirely *inside* `AxiContentionTop` — there is no `sc_port` exposed for anything
  outside the VP to bind to. Connecting this to a real external AXI subordinate/master (RTL,
  a verification IP, or another SystemC block) would need that port built first, and has never
  been tested against anything but the VP's own internal slave.

### Build & run

```bash
mkdir -p build_cycle6_axi && cd build_cycle6_axi
cmake .. -DTIMING_MODEL=CYCLE6_AXI -DCMAKE_BUILD_TYPE=Release   # or CYCLE6_AXI128/256/512/1024 for wider AXI
make -j$(nproc)
cd ..

export LD_LIBRARY_PATH=build_cycle6_axi:build_cycle6_axi/systemc/src:build_cycle6_axi/spdlog
./build_cycle6_axi/RISCV_VP -R 64 -f tests/hex/robust_fast64.hex --max-instr 3000000
# comparison-mode examples (same binary):
VP_CACHE_POLICY=LRU ./build_cycle6_axi/RISCV_VP -R 64 -f tests/hex/robust_fast64.hex --max-instr 3000000
VP_AXI_BEATS=1 ./build_cycle6_axi/RISCV_VP -R 64 -f tests/hex/robust_fast64.hex --max-instr 3000000
VP_DCACHE_SLOTS=8 ./build_cycle6_axi/RISCV_VP -R 64 -f tests/hex/robust_fast64.hex --max-instr 3000000
```

The `-DTIMING_MODEL=CYCLE6_AXI`/`CYCLE6_AXI128` binaries run any bare-metal `.hex` the same way
as the default build. The specific stress test used to derive the results below was a dedicated
I$+D$ concurrent-thrash workload built during calibration; it is not included in this repo, but
any memory-heavy bare-metal program will exercise the same contention path.

### Verified results (I$+D$ concurrent-contention stress test)

| | CVA6 RTL | VP — No AXI | VP — With AXI (2-beat burst) |
|---|---:|---:|---:|
| Cycles | 4,334,798 | 4,111,121 | **4,266,611** |
| IPC | 0.378 | 0.399 | **0.384** |
| Error vs RTL | — | −5.2% | **−1.57%** |

Once both the underlying miss-latency calibration and the AXI subsystem's own separate
`axi_slave_latency` constant were correctly derived and kept in sync, real AXI contention
modelling with genuine burst semantics measurably improved accuracy over the no-AXI baseline.
Full investigation, including the earlier (now-resolved) finding that a naively-calibrated AXI
model made things *worse*, is documented in `docs/BUGS.md` §B7/§B10.

---

## Comparison with Real CVA6

| Metric | This VP | Real CVA6 |
|--------|---------|-----------|
| Pipeline depth | 6 stages | 6 stages |
| Issue width | 1 (in-order) | 1 (in-order) |
| I-cache | 16 KB / 4-way / 16 B / LFSR | 16 KB / 4-way / 16 B / LFSR ✓ |
| D-cache | 32 KB / 8-way / 16 B / LFSR | 32 KB / 8-way / 16 B / LFSR ✓ |
| BTB / BHT / RAS | 32 / 128 / 2 | 32 / 128 / 2 ✓ |
| ITLB / DTLB | 16 / 16 | 16 / 16 ✓ |
| Load-use stall | 2 cycles ✓ (3-cycle hit latency, pipelined) | 2 cycles (measured) |
| Load→address (gather) latency | +2 cycles ✓ | ~2 extra cycles, verified via isolated pointer-chase (0.006% error) |
| MUL latency | 2 cycles ✓ | 2 cycles |
| AXI data width (optional `CYCLE6_AXI` mode) | 64-bit, 2-beat bursts ✓ (128/256/512/1024-bit builds also available, comparison-only) | 64-bit, 2-beat bursts (per-line refill) |
| sv39 MMU | Yes ✓ | Yes |
| L2 cache | Not modelled | 256 KB unified (optional) |
| Linux boot IPC | **0.615** (136.6M instr / 222.2M cycles) | No RTL reference (the co-sim testbench cannot boot Linux) |

### Validation against CVA6 RTL (frozen default configuration, AXI build)

Same ELF on the VP and the CVA6 RTL co-simulation (`systemc/build-system-strobefix/Vcva6`), each
benchmark's own timed region (its CSR counter readings) on both sides:

| Benchmark | RTL cycles | VP cycles | Error |
|---|---:|---:|---:|
| int_alu / int_nobranch / int_indep | 2,400,417 / 3,080,384 / 2,249,473 | 2,400,130 / 3,080,096 / 2,249,183 | −0.01% each |
| long_test3 / robust_stress / multiply | 1,100,413 / 4,567,925 / 3,279,837 | 1,100,124 / 4,567,638 / 3,279,077 | −0.03% / −0.01% / −0.02% |
| div_stress | 1,130,126 | 1,149,840 | +1.74% |
| fp_bench / fp_stress | 2,600,478 / 6,200,440 | 2,609,563 / 6,400,140 | +0.35% / +3.22% |
| CoreMark (2K perf run, 10 iter., CRCs validated) | 5,121,158 | 5,243,331 | +2.39% |
| Dhrystone (2000 runs) | 1,113,360 | 1,094,827 | −1.66% |
| median | 4,159,515 | 4,209,243 | +1.20% |
| SpMV, double precision | 99,142 | 99,017 | −0.13% |
| SpMV, integer | 1,262,614 | 1,287,672 | +1.98% |
| ptr_chase | 3,205,274 | 3,205,500 | +0.01% |
| cache_sweep (2 MB cold + 4 KB hot) | 10,283,200 | 10,163,638 | −1.16% |
| locality (96 KB cold + 4 KB hot) | 7,439,113 | 6,898,872 | −7.26% |
| mem_contention (I$+D$ stress) | 4,334,798 | 4,554,375 | +5.07% |

Branch mispredict counts match the RTL within 0.7% on every benchmark. Full details and the
measurements behind each calibration: `docs/BUGS.md` §B11–§B15.

### Known residual gaps (honestly stated, not yet closed)

1. **FP residuals:** compare/convert → integer use and FP load → use are within ±5% on
   microbenchmarks; fp_stress +3.2% (`docs/BUGS.md` §B16).
2. **Divider timing:** the RTL makes a same-register divide wait for the previous one to commit
   but overlaps result use with that wait; the VP does the opposite and over-charges trivial
   divides (div_stress +1.7%, §B15).
3. **Misses interleaved with hits / I$+D$ overlap:** locality −7.3% with identical miss counts
   (about 2 cycles per miss under-charged), mem_contention +5.1% (§B7, §B15, §B16).
4. **No L2 cache.**

#### Divider validation (vs CVA6 RTL co-simulation)

A register-only 64-bit `div`/`rem` stress benchmark, bit-exact result agreement:

| Divider model | Cycles | IPC | avg cyc/divide | Error vs CVA6 |
|---------------|-------:|----:|---------------:|--------------:|
| CVA6 RTL (reference) | 1,130,126 | 0.165 | ~47 | — |
| VP — flat 66-cycle (old) | 1,483,431 | 0.126 | 65 | +31% |
| **VP — operand-value early-termination (new)** | **1,123,900** | **0.166** | **47** | **−0.6%** |

---

## Known Limitations

| Limitation | Notes |
|------------|-------|
| FP, divider and I$/D$-overlap residuals | See "Known residual gaps" above |
| No L2 cache | D$ misses resolve directly to a flat penalty rather than escalating through an L2 |
| `CYCLE6_AXI` covers only I$/D$ read contention | Store-drain and DMA contention deliberately not modelled — both evaluated and found unnecessary or unvalidatable, see the AXI section above |
| RV32 6-stage model is not updated | Lacks cache model, CSR_File, MMU — suitable for bare-metal RV32 only |
| No write-combining | Stores drain one at a time; real HW coalesces adjacent stores |
| Single-core | No cache coherence protocol needed or modelled |

---

## Build Instructions

### Dependencies

- CMake ≥ 3.10
- C++17 compiler (GCC 9+, Clang 10+; verified with GCC 13)
- Nothing else — SystemC and spdlog are vendored; matchlib (for `CYCLE6_AXI` only) is vendored too

### Build — default cycle-accurate model

```bash
mkdir -p build_cycle6 && cd build_cycle6
cmake .. -DTIMING_MODEL=CYCLE6 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
```

### Build — AXI contention model (optional)

```bash
mkdir -p build_cycle6_axi && cd build_cycle6_axi
cmake .. -DTIMING_MODEL=CYCLE6_AXI -DCMAKE_BUILD_TYPE=Release   # or CYCLE6_AXI128/256/512/1024 for wider AXI
make -j$(nproc)
cd ..
```

### Rebuild after source changes

```bash
cd build_cycle6 && make -j$(nproc)      # or build_cycle6_axi
```

---

## Running

Set the library path once per shell before running any binary (adjust the build dir name for
whichever target you built):

```bash
export LD_LIBRARY_PATH=build_cycle6:build_cycle6/systemc/src:build_cycle6/spdlog
```

### Linux boot

```bash
./build_cycle6/RISCV_VP -R 64 \
  --bios  boot/fw_jump.bin \
  --dtb   boot/vp.dtb \
  --kernel boot/Image \
  --initrd boot/initramfs.cpio.gz
```

### Linux boot with a snapshot of stats exactly at the shell prompt

```bash
VP_BOOT_STATS=1 ./build_cycle6/RISCV_VP -R 64 \
  --bios  boot/fw_jump.bin \
  --dtb   boot/vp.dtb \
  --kernel boot/Image \
  --initrd boot/initramfs.cpio.gz
# ... reaches "~ #" and keeps running (idle-loops forever) — Ctrl+C once the
# "Boot Pipeline Statistics" block has printed.
```

### Bare-metal hex program (RV64)

```bash
./build_cycle6/RISCV_VP -R 64 -f tests/hex/robust_fast64.hex --max-instr 3000000
```

### Bare-metal hex program (RV32)

```bash
./build_cycle6/RISCV_VP -R 32 -f program.hex
```

---

## Project Structure

```
RISCV-VP/
├── src/
│   ├── CPU_P64_6_Cycle.cpp   # 6-stage CVA6-aligned pipeline
│   ├── CPU_P32_6_Cycle.cpp   # 6-stage RV32 pipeline (bare-metal only)
│   ├── VPTop.cpp             # SoC top-level wiring
│   ├── VPMain.cpp            # Main entry point, CLI parsing
│   ├── BusCtrl.cpp           # TLM bus + address decode
│   ├── Memory.cpp            # 512 MB flat memory model
│   ├── BASE_ISA.cpp          # Shared ISA execution handlers
│   └── axi/
│       └── AxiContentionTop.cpp  # Real matchlib AxiArbiter integration (CYCLE6_AXI only)
├── inc/
│   ├── CPU_P64_6_Cycle.h     # Pipeline state, latches, stats structs
│   ├── Scoreboard.h          # CVA6-aligned scoreboard / ROB (32 entries)
│   ├── StoreBuffer.h         # Split speculative/committed store buffer
│   ├── Cache.h               # N-way set-associative cache; LFSR (matches CVA6) + LRU/MRU/PLRU/NRU/LFU comparison policies
│   ├── MMU.h                 # sv39 ITLB + DTLB + page table walker
│   ├── CSR_File.h            # Full M/S/U CSR file + interrupt logic
│   ├── CLINT.h               # Core-local interrupt controller
│   ├── PLIC.h                # Platform-level interrupt controller
│   ├── UART.h                # 16550 UART peripheral
│   ├── TLB.h                 # TLB entry and flush logic
│   ├── VPTop.h                # SoC composition
│   └── axi/
│       ├── AxiContentionTop.h    # Matchlib-free interface (PIMPL) — the CPU never sees matchlib
│       ├── AxiRefillMaster.h     # Per-source (I$/D$) AXI manager, 2-beat burst reads
│       └── MinimalAxiMemSlave.h  # Array-backed AXI subordinate, configurable latency
├── third_party/
│   └── matchlib_kit/          # Vendored matchlib (Stuart-Swan Accellera kit), CYCLE6_AXI only
├── tools/
│   ├── cache_sweep/            # Fast RuntimeCache hit-rate model (self-checked bit-identical to
│   │                            #   the production Cache<>), no VP rebuild needed: D$/I$ sets,
│   │                            #   ways, line-width, and policy-vs-Belady's-OPT sweeps
│   ├── axi_burst_stress/       # Standalone AXI bulk-transfer throughput tool (bypasses CPU/cache
│   │                            #   entirely), block-size × AXI-width sweep
│   └── axi_external_demo/      # Proves the internal AXI master can drive a genuine external
│                                #   port at any width, real D$ + real captured trace
├── boot/
│   ├── fw_jump.bin           # OpenSBI M-mode firmware
│   ├── vp.dtb                # Device tree blob for this VP
│   ├── Image                 # Linux 6.1.0 kernel image
│   └── initramfs.cpio.gz     # BusyBox root filesystem
├── systemc/                   # Vendored SystemC 3.0.2 (no external install needed)
├── tests/hex/                 # Bare-metal Intel-HEX test binaries (only robust_fast64.hex and
│                               #   dhrystone32.hex are tracked; the rest are local build output)
├── docs/
│   ├── BUGS.md                 # Primary, current bug/calibration log — start here
│   ├── BUGS_AND_ISSUES.md      # Earlier bug log (pre-dates the calibration work in BUGS.md)
│   ├── Technical_Report.md     # technical summary
│   └── Project_Overview.md     # Structural overview
└── dts/
    └── riscv_vp.dts            # Device tree source
```

---

## Bug & Calibration History

The primary, current record of every bug, timing-model calibration gap, and validation result
is [`docs/BUGS.md`](docs/BUGS.md) — organised as a lettered/numbered log (§A, §B1–§B11+),
each entry with symptom, root cause, fix, and the RTL co-simulation numbers used to verify it.
Highlights from the most recent calibration pass:

| § | Finding | Result |
|---|---|---|
| B6 | Branch-misprediction flush penalty was one cycle too cheap — the frontend-redirect bubble already charged on correctly-predicted taken branches (§B2) was never applied on the misprediction/flush path | `robust_stress` IPC error +2.8% → **~0.0%**, all seven control benchmarks unaffected. **⚠ But `flush_pipeline` covers 33 different redirect reasons (traps/interrupts/faults, not just mispredictions), and the fix applies the bubble to all of them uniformly, validated only against the branch case** — Linux boot IPC regressed 0.597 → 0.584 as a result. Real win on the isolated case, real regression on the full-system one; not yet re-scoped. See §B6. |
| B7 | AXI contention modelling investigated; store-drain and DMA contention evaluated and found unnecessary/unvalidatable | See "AXI Contention Model" above |
| B8 | Cache replacement was LRU; CVA6 uses LFSR pseudo-random — LRU pathologically over-misses on scans larger than the cache | Miss counts corrected from ~22% error to <0.2% |
| B9 | Load→address (gather/pointer-chase) dependency latency was under-modelled by ~2 cycles | Isolated pointer-chase test now matches RTL to 0.006% |
| B10 | I$ miss latency was miscalibrated (10 cycles, should be 7); AXI burst mode implemented and its own latency constant re-derived to match | Memory-bound divergence reduced from up to +38.8% to as low as −1.57% (with recalibrated AXI burst mode) |
| B11 | D$ hit latency: CVA6's own design doc states 3 cycles, VP models 1 — tested empirically (1/2/3 all measured against real RTL cycles), value 2 lands closest but overshoot/undershoot pattern suggests the real cause is the VP's `load_hit_fu` being a single blocking slot (can't hide latency behind independent work the way CVA6's scoreboard can), not the wrong flat constant | **Open** — root cause hypothesis identified, not yet fixed; see `docs/BUGS.md` §B11 |

An earlier, separate 24-bug log covering the original Linux-boot bring-up (reset/init, ISA
correctness, store buffer, MMU/TLB, privilege/interrupts, peripherals) is preserved in
[`docs/BUGS_AND_ISSUES.md`](docs/BUGS_AND_ISSUES.md) for historical reference.

---

## Acknowledgements

Built on top of the [RISC-V TLM Simulator](https://github.com/mariusmm/RISC-V-TLM)
originally developed by **Màrius Montón**. The TLM-2.0 infrastructure, memory model,
and base ISA handlers originate from that work.

The optional AXI contention model uses **NVIDIA matchlib**, via the public
[Stuart-Swan Matchlib-Examples-Kit-For-Accellera-Synthesis-WG](https://github.com/Stuart-Swan/Matchlib-Examples-Kit-For-Accellera-Synthesis-WG),
vendored in `third_party/matchlib_kit/`.

Extensions added in this project:
- Complete CVA6-aligned 6-stage cycle-accurate pipeline
- sv39 MMU with ITLB, DTLB, and hardware page table walker
- Full M/S/U privilege architecture and CSR file
- L1 I/D cache model, RTL-matched geometry and LFSR replacement policy, plus five
  comparison-mode policies (LRU/MRU/PLRU/NRU/LFU) independently switchable per cache at runtime
- Load-use and load→address dependency hazard detection
- CLINT, PLIC, UART peripherals wired for Linux
- Store buffer with partial-overlap stall
- Branch predictor (BTB + BHT + RAS), RTL-matched sizing
- Linux 6.1.0 boot support
- Optional real-AXI (matchlib `AxiArbiter`) I$/D$ contention model with genuine burst semantics

## License

GNU General Public License v3.0 — see `LICENSE` for details.
