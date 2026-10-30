# RISC-V VP — Bugs Found & Timing-Model Calibration (CVA6 co-simulation validation)

> **This is a lab notebook, in the order the work was done.** Later sections
> correct earlier ones, and some early numbers (accuracy tables, the "shift
> bubble", LRU caches) are superseded. The final model's numbers are in
> [tests/reference/reference.csv](../tests/reference/reference.csv) and §B15–§B17.

This document records every functional bug and timing-model calibration gap found
while validating the 6-stage cycle-accurate VP (`CPU_P64_6_Cycle`) against the
**CVA6 RTL co-simulation** (`cv64a6_imafdc_sv39`, Verilated + matchlib/SystemC).

**Method.** Each functional unit was isolated with a **register-only, single-
predictable-branch bare-metal microbenchmark** (no memory, no random branches, no
cold-start), run on both the VP and the CVA6 co-sim with the *same binary*.
Every benchmark emits its HPM counters and the **bit-exact result word**, so both
numerical correctness and cycle/IPC accuracy are checked. Benchmarks:
`int_alu_bench` (ALU), `fp_bench` (FP add/mul), `fp_stress` (FP add/mul/div/sqrt),
`div_stress` (integer div/rem), plus `robust_stress` (branches), `long_test3`
(loads), `cam_bench`.

---

## A. Functional (correctness) bugs

### A1. FP load/store immediate decoded as 0 — **FPU produced garbage** *(FIXED)*
- **Symptom:** every floating-point program produced wrong results (e.g. `2.0+3.0`
  returned `1.56e-286`). Short FP loops also failed to terminate (control flow
  derailed by corrupted FP-compare operands).
- **Root cause:** the standard-decode immediate switch handled the integer load
  `0x03` and store `0x23`, but **not the FP load `0x07` or FP store `0x27`**, so
  `fld/fsd` fell to `default: imm = 0`. `fld fa5, 72(a5)` computed address
  `a5 + 0` → read the **wrong memory** → garbage operands. (Offset-0 loads worked
  by luck, which is why the bug hid for a long time.)
- **Fix:** add `case 0x07` to the I-type immediate group and `case 0x27` to the
  S-type group in `PCGen/decode` (`CPU_P64_6_Cycle.cpp`), plus `0x27` to the
  no-`rd` group and `0x07` to the rs2-mask group. Commit *"Decode FP load and
  store instructions correctly in standard decode stage."*
- **Validation:** `2.0+3.0 → 0x4014000000000000` (5.0); `fp_stress` result now
  bit-exact vs CVA6 (`0x3ff7c6176c452d17`). Everything else in the FP datapath
  (F/D decode dispatch, `f_regs` sharing, commit FP/int routing) was already
  correct.

### A2. FP loads bypassed the D-cache model entirely *(FIXED)*
- **Symptom:** `fp_bench` reported **CVA6 6 D$ misses / 47 accesses vs VP 0 / 0**,
  despite both running the same binary — which contains 10 `fld` instructions that
  load the FP constants (`0.9999`, `0.1`, …) from `.rodata`.
- **Root cause:** `dcache.access()` was called from **exactly one site** in the
  whole VP (`CPU_P64_6_Cycle.cpp`, integer-load path, opcode `0x03`). The FP load
  path (opcode `0x07`) reaches memory through `D_extension.h:113`
  (`mem_intf->readDataMem64()`), which never touches the cache model. So in the VP
  an `fld` was a **free** memory access: no access counted, no miss counted, and
  **no `dcache_miss_penalty` charged**. Real CVA6 has a single LSU — integer and FP
  loads share the same L1 D$.
- **Fix (two parts — the second is easy to miss):**
  1. Probe `dcache.access(pa)` on the FP-load path once the physical address is
     resolved (after MMU translation and the PMP check), and on a miss defer
     completion through `dcache_miss_fu` exactly as the integer path does. The FP
     register file is already written by `execute()`; only the *timing* is modelled.
  2. **Add an Issue-stage structural guard for opcode `0x07`.** `dcache_miss_fu` is
     a **single slot**, and the existing guard only covers `fu == LSU` with opcode
     `0x03`/`0x2F`. FP loads are classified as `FunctionalUnit::FPU`, so without a
     new guard a second FP load issues while the first miss is in flight,
     **overwrites `dcache_miss_fu`, and the first load's scoreboard entry never
     completes — the ROB head wedges and the VP hangs forever.** Part 1 alone hangs
     `fp_bench` (10 back-to-back `fld`s) on the first run.
- **Scope / deliberate omissions:** FP **stores** are still not counted — but
  neither are integer stores (both are write-through and handled by the store
  buffer), so the model stays internally consistent. Loads are what the D$ counters
  track in this VP.
- **Validation (measured, whole suite re-run):** `fp_bench` D$ accesses **0 → 10**
  (exactly the 10 `fld`s in the binary), misses **0 → 2**. Cycles
  2,510,107 → 2,510,207 (**+100**, 0.004%); `fp_stress` 5,900,516 → 5,900,619
  (**+103**). **The other six benchmarks are cycle-for-cycle identical**, and every
  result word is unchanged. IPC unmoved to three decimals.
  - VP reports 2 misses where CVA6 reports 6: the same line-size ratio as the I$
    (VP 64-byte lines vs CVA6 16-byte lines) over the same ~100 bytes of constants.
- **Impact on prior results: negligible, and that is the point.** The affected
  benchmarks were designed to be register-only, so the constants load a handful of
  times outside the hot loop. **This bug does not explain fp_bench's +3.6% error**
  (see §B3) and fixing it does not close that gap — B3's fitted FPnew latencies own
  that residual.
- **Why it matters anyway:** any FP workload that *streams* data from memory —
  i.e. most real FP code — would have had its memory latency silently dropped. The
  microbenchmarks hid the bug precisely because they were built to avoid memory.

### Note: numerical correctness of all units
After A1, **every** benchmark's result word matches the CVA6 co-sim **bit-for-bit**
(int `0x3fb776e12465e67d`, fp_bench `0x4184463b0cf87061`, fp_stress
`0x3ff7c6176c452d17`, div `0x5f6dd0b692933335`). The FP path uses native host
IEEE-754 (`std::fma`, rounding via `fesetround`); it matches FPnew for normal
values (NaN-boxing / signalling-NaN / subnormal edge cases are not separately
verified).

---

## B. Timing-model calibration gaps (each measured, then modelled)

### B1. Divider: flat 66-cycle → **operand-value early termination** *(FIXED)*
- **Symptom:** `div_stress` cycles **+31%** vs CVA6 (VP IPC 0.126 vs 0.165).
- **Root cause:** every 64-bit divide was charged a flat **66 cycles**; CVA6's
  serial divider terminates early (latency ∝ significant quotient bits), averaging
  ~47 cycles on this workload.
- **Fix:** data-dependent latency `div_shift = LZC(divisor) − LZC(dividend);
  cycles = div_shift + 6` with 1-cycle fast paths for ÷0 / ÷(−1) / zero-quotient,
  clamped to 66/34. (`+6` tuned to CVA6's ~47-cycle average.)
- **Result:** cycle error **+31% → −0.6%** (VP 1,123,900 vs CVA6 1,130,126, IPC
  0.166 vs 0.165), result bit-exact.

### B2. Taken-branch: zero-cost redirect → **1-cycle fetch bubble** *(FIXED)*
- **Symptom:** `int_alu` ran at a *perfect* IPC 1.000 while CVA6 was 0.917 (**+9%**).
- **Root cause:** a correctly-predicted **taken** branch set `next_pc = target`
  with **no cost**; CVA6 pays a 1-cycle BTB redirect bubble (~1 cyc per taken
  branch → ~200K extra cycles on a 200K-iteration loop).
- **Fix:** `taken_redirect_bubble` flag — PCGen injects one fetch bubble after each
  predicted-taken branch/jump (cleared on flush so it never double-counts
  mispredicts).
- **Result:** `int_alu` **1.000 → 0.916** (CVA6 0.917, −0.1%). Correctly a
  *no-op* on load-/div-/FP-bound code (the bubble hides behind backend stalls).

### B3. FPU: single blocking unit → **pipelined add/mul, blocking div/sqrt** *(FIXED)*
- **Symptom:** `fp_bench` (add/mul) **−21%** vs CVA6 (IPC 0.394 vs 0.500).
- **Root cause:** the FPU was one blocking unit — every FP op held it for its full
  latency, so *independent* FP ops could not overlap. CVA6's FPnew ADDMUL block is
  **pipelined** (throughput 1/cycle); only its DIVSQRT block is iterative/blocking.
- **Fix:**
  1. Replace the single `fpu_fu` with an 8-slot **pipelined** unit `fpu_pipe[]`
     (throughput 1; Issue stalls only when all slots busy; dependent consumers
     stall via the scoreboard).
  2. Route **fdiv/fsqrt** (latency > 5) to a dedicated **non-pipelined blocking**
     unit `fpu_divsqrt_fu` (a 2nd div/sqrt waits for the first) — mirrors FPnew.
  3. Raise ADDMUL latency 3 → **4** (CVA6 FPnew double-precision add/mul depth).
- **Result:** `fp_bench` **0.394 → 0.518** (CVA6 0.500, +3.6%); `fp_stress`
  (add/mul/div/sqrt) **0.152** (CVA6 0.145, +4.8%); results bit-exact.
- **Pitfall recorded:** the first cut pipelined *everything*, which wrongly sped up
  div/sqrt (`fp_stress` 0.250 vs 0.145). Splitting ADDMUL (pipelined) from DIVSQRT
  (blocking) is essential.

- **Follow-up: isolated per-op sweep against RTL (`tests/fpmb/`).** Five pure
  single-op double-precision microbenchmarks (dependent FADD, FMUL, FDIV, FSQRT
  chains; independent FADD for throughput), each run as the *same ELF* on the VP and
  the CVA6 RTL co-sim (8 unrolled ops/iteration, loop overhead hidden):

  | Benchmark | RTL cyc/op | VP cyc/op (4/20/22) |
  |---|---:|---:|
  | FADD.D dependent chain | 5.00 | 4.00 (−20%) |
  | FMUL.D dependent chain | 5.00 | 4.00 (−20%) |
  | FADD.D independent (throughput) | 11.05 /iter | 11.02 /iter (−0.2%) |
  | FDIV.D dependent chain | 24.0 | 20.0 (−16.7%) |
  | FSQRT.D dependent chain | 24.0 | 22.0 (−8.4%) |

  Pipelined throughput is already right; only dependent-chain latency differs.
  **Setting the flat latencies to 5/24/24 makes the chains match RTL to 0.01% but
  overshoots the aggregate benchmarks:** `fp_bench` 2,912,749 vs RTL 2,600,478
  (+12%), `fp_stress` 6,800,207 vs 6,200,440 (+9.7%); with only DIV/SQRT=24,
  `fp_stress` flips from −4.8% to +4.8% (RTL sits midway). Conclusion: CVA6 pays a
  ~1–2 cycle producer→consumer handoff on top of each unit's latency in back-to-back
  chains, which mixed code partly hides; the VP has no such mechanism, so its
  constants (4/20/22) were fitted to the aggregate and absorb it. A flat-constant
  re-fit would just be fitting one benchmark, so **constants were left unchanged**.
  The real fix is modelling the handoff (extra cycle only when a consumer is already
  stalled on the producer) and validating on held-out mixed FP code. Open.

### B4. Shift→adder dependency bubble missing *(FIXED)*
- **Symptom:** on a *branch-free*, fully-unrolled ALU dependency chain
  (`int_nobranch`), the VP ran at IPC **0.996** while CVA6 was **0.833** — a
  **+19.5%** error. Not explained by branches (1 mispredict), I$ misses (324, all
  cold), or memory (D$ = 0/0). ~478,000 cycles (**0.19 cyc/instr**) unaccounted.
- **Diagnosis — the discriminating experiment.** Two hypotheses fit the number
  equally well: (H1) a back-to-back dependency bubble, or (H2) a fetch-bandwidth /
  I$ line-crossing limit. A control benchmark `int_indep` was built with **identical
  straight-line structure and code size** but the dependencies **spaced 8 apart**
  (8 interleaved independent variables). Result:

  | Benchmark | deps | CVA6 CPI | VP CPI | verdict |
  |---|---|---:|---:|---|
  | `int_nobranch` | back-to-back | **1.200** | 1.00 | gap present |
  | `int_indep` | spaced out | **1.002** | 1.00 | gap gone |

  CVA6's CPI collapsed to 1.002 with the *same fetch pattern* → **H1 confirmed,
  H2 refuted.** Penalty quantified: 514,345 extra cycles ÷ 513,207 dependent pairs
  = **1.002 cycles per dependent pair — exactly one.**
- **Root cause:** in the `-O1` unrolled body, `d = d + (a << 3)` compiles to
  `slli t,a,3` immediately followed by the dependent `add d,d,t`. Real CVA6 cannot
  forward a **shift** result to the very next arithmetic/branch/store consumer
  without one bubble; the VP forwarded it for free.
- **Fix:** in `Issue_stage`, when the instruction in EX is a **shift**
  (`SLL/SRL/SRA` + I/W variants) and the instruction in Issue reads its `rd`,
  insert a 1-cycle stall — **unless** the consumer is a fast logical op
  (`AND/OR/XOR`), which CVA6 can bypass.
- **Result:** `int_nobranch` **0.996 → 0.831** (CVA6 0.833, **−0.2%**), with **no
  regression** on the controls (`int_indep` 0.996 vs 0.998; `int_alu` 0.916 vs
  0.917). Results bit-exact.
- **Why it hid for so long:** in a *small* loop GCC schedules the shift away from
  its consumer, so the penalty rarely fires — the looped `int_alu` test agreed
  (0.916 vs 0.917) partly by coincidence. Only the fully-unrolled, branch-free
  variant exposed it. **Lesson: build a branch-free control for every unit.**

### B5. Load-use stall over-charge *(FIXED)*
- **Symptom:** `long_test3` (load-heavy `-O1`) cycles **+100%** / IPC **−50%**
  (VP 0.364 vs CVA6 0.727).
- **Root cause:** two memory penalties were set to model *off-chip AXI/DRAM*
  latency, but the co-sim models **no main memory latency whatsoever** — its
  `dram` instance is a zero-wait-state behavioural `SlaveFromFile` (see §C) — and
  CVA6's L1 D$ hit latency is a single cycle:
  - `load_hit_penalty = 5` — charged as a **structural stall on every load** (arms
    `load_hit_fu`; Issue stalls while busy), regardless of whether the next
    instruction uses the result.
  - `store_write_penalty = 20` — write-through drain latency per store.
- **Fix:** `load_hit_penalty` **5 → 1** (L1 D$ hit latency) and
  `store_write_penalty` **20 → 0** (`CPU_P64_6_Cycle.h:358-359`).
- **Result:** `long_test3` VP **0.364 → 0.727** vs CVA6 **0.727** — an **exact
  match** (800,056 instr / 1,100,602 cycles). Stall breakdown now shows 299,999
  load-use stalls at 1 cycle each, and stores drain in-line.
- **⚠ Caveat — this calibrates to a testbench stub, not to silicon.**
  `store_write_penalty = 0` and `load_hit_penalty = 1` are correct *against the CVA6
  co-sim harness*, whose "memory" is a zero-wait-state `std::map` with no timing
  model (§C). On a real SoC with DRAM behind AXI, both are badly optimistic. The
  perfect long_test3 agreement (0.727 vs 0.727) therefore demonstrates that **the VP
  matches the RTL harness**, not that it predicts silicon performance. If
  `SlaveFromFile` is ever replaced with a timed memory model, **these two constants
  must be re-derived**; they no longer represent any physical latency.
- **Note on the original diagnosis:** the "blanket structural stall vs scoreboard
  dependency" analysis above is still *architecturally* accurate — `load_hit_fu` is
  a single blocking slot, so two independent loads cannot overlap. It simply stopped
  being *observable* once the penalty dropped to 1 cycle, because a 1-cycle blocking
  unit and a 1-cycle scoreboard dependency are indistinguishable in cycle count.
  A workload with two independent loads and enough independent work between them
  would still expose it. Not worth fixing until such a workload matters.

### B6. Branch-misprediction flush penalty one cycle too cheap *(FIXED for isolated branch mispredicts, uncommitted; boot IPC vs the stale 0.597 reference is not a valid comparison — see follow-up)*
- **Symptom:** `robust_stress` (branch-heavy: multiply-accumulate driven by an LCG
  random number generator, deliberately unpredictable) — VP IPC **0.704** vs CVA6
  **0.685**, i.e. the VP was **+2.8% optimistic** (it ran *faster* than the RTL).
- **Diagnosis — the counters isolated it exactly.** No new benchmark was needed;
  the existing HPM counters separate *rate* from *penalty*:

  | Quantity | CVA6 | VP (before fix) |
  |---|---:|---:|
  | Instructions retired | 3,127,046 | 3,127,063 |
  | Cycles | 4,567,925 | 4,442,704 |
  | Branch mispredicts (HPM4) | 125,498 | 125,500 |

  Mispredict **counts agreed to 2 parts in 125,000** → the branch predictor's
  *accuracy* was modelled correctly, so the gap was not a prediction-rate error.
  The cycle shortfall was `4,567,925 − 4,442,704 = 125,221`, and

  > **125,221 missing cycles ÷ 125,498 mispredicts = 0.998 cycles per mispredict**

  — **exactly one cycle.** The VP's misprediction recovery was one cycle shorter
  than CVA6's, accounting for **100%** of the residual.
- **Root cause:** the VP flushed and redirected from EX in one cycle
  (`flush_pipeline` → `prev_cycle_flush` → refetch), with no bubble. Real CVA6
  takes one additional cycle to steer the frontend after the flush — the same
  frontend-redirect latency already modelled for *correctly-predicted taken*
  branches (§B2, `taken_redirect_bubble`), which had never been applied on the
  mispredict/flush path.
- **Fix (`PCGen_stage`, `CPU_P64_6_Cycle.cpp`):** the `if (flush_pipeline)` block
  now sets `taken_redirect_bubble = true` (previously `false`) before redirecting
  `next_pc`, charging the same one-cycle bubble §B2 already charges on the
  predict-taken path, consumed by the existing `if (taken_redirect_bubble)` check
  the very next cycle.
- **A second, real bug surfaced while validating the fix.** The pipeline-empty
  termination check (`stats.cycles > 100 && !this_cycle_flush && ...`) only
  excluded the flush cycle itself, not the new bubble cycle that follows it. A
  flush drains the whole pipeline (`fetch_queue.clear()`, all fetch/id/issue
  latches invalidated), so the bubble cycle right after a flush looked
  indistinguishable from "simulation done" to that check — `robust_stress` halted
  after **34 instructions** (first mispredict) instead of ~3.1M on the first test
  of the fix. Fixed by capturing `taken_redirect_bubble` into a
  `this_cycle_bubble` local **before** `PCGen_stage()` consumes/clears it (mirrors
  the existing `this_cycle_flush` capture pattern one line above), and adding
  `!this_cycle_bubble` to the termination guard.
- **Verified — robust_stress lands almost exactly on RTL:**

  | | CVA6 RTL | VP (after fix) | Error |
  |---|---:|---:|---:|
  | IPC | 0.685 | **0.685** | **~0.0%** |
  | Cycles | 4,567,925 | 4,567,716 | **−0.005%** |

- **Verified — all seven control benchmarks re-run, none broke.** The predicted
  "should not move" benchmarks (3–5 mispredicts each) shifted by <0.3%; the one
  control with a real, documented mispredict count (`div_stress`, ~4,000 from its
  one data-dependent branch) shifted by **+3,426 cycles** — matching its own
  mispredict count almost exactly, confirming the fix's magnitude is correctly
  scoped to mispredicts and nothing else:

  | Benchmark | VP cycles (before) | VP cycles (after) | Δ |
  |---|---:|---:|---:|
  | int_alu | 2,400,714 | 2,400,216 | −498 |
  | int_nobranch | 3,086,804 | 3,080,175 | −6,629 |
  | int_indep | 2,254,211 | 2,249,280 | −4,931 |
  | div_stress | 1,126,502 | 1,129,928 | **+3,426** (≈ its ~4,000 mispredicts) |
  | long_test3 | 1,100,602 | 1,100,186 | −416 |
  | fp_bench | 2,510,207 | 2,509,625 | −582 |
  | fp_stress | 5,900,619 | 5,900,208 | −411 |

- **Supersedes the retracted "lockstep frontend" explanation** (see §D), which was
  wrong on both mechanism and sign.
- **⚠ Follow-up: Linux boot IPC regressed 0.597 → 0.584, and scoping did not fix it.**
  `flush_pipeline` has 33 set-sites (mispredicts, traps, interrupts, faults), so the
  bubble was first suspected of over-charging non-branch redirects. It is now gated by
  `flush_is_branch_mispredict`, set only at the three sites that count a branch/jump
  mispredict (`stats.branch_mispredicts++`). Result: `robust_stress` and all 7 controls
  are cycle-identical, but Linux boot only moved 237,427,165 → 236,907,845 cycles
  (IPC still **0.584**). In the boot, `Flushes == Mispredicts == 8,288,083`, i.e.
  essentially every flush already *is* a branch mispredict, so the trap hypothesis is
  refuted. The +~6.5M boot cycles are the bubble on those 8.3M mispredicts
  (predict rate only 55.7%). Open question: either the VP mispredicts far more often
  than CVA6 does on kernel code (predictor divergence, e.g. `ras.clear()` on every
  mispredict is a VP-only recovery strategy), or the 0.597 paper figure is not
  comparable. Needs an RTL Linux-boot mispredict count to decide.
  A run with `ras.clear()` removed gave a boot **bit-identical** to the scoped run
  (same cycles and mispredict count); that is implausible for a real predictor change
  and was not investigated further, so treat the RAS hypothesis as *inconclusive*, not
  ruled out.
- **A/B confirmation (2026-09-28), `CYCLE6_AXI` build:** added a `VP_NO_B6_BUBBLE`
  diagnostic env var (`src/CPU_P64_6_Cycle.cpp`, gates
  `taken_redirect_bubble = flush_is_branch_mispredict`) and booted Linux both ways:

  | Run | Instructions | Cycles | IPC |
  |---|---:|---:|---:|
  | §B6 bubble on (current default, already scoped to real mispredicts) | 139,613,943 | 238,118,336 | 0.586 |
  | §B6 bubble off entirely (`VP_NO_B6_BUBBLE=1`) | 139,060,558 | 229,130,211 | **0.607** |
  | Paper reference (§VI-F, commit `28f04b0`) | 137,936,875 | 230,924,306 | 0.597 |

- **⚠ Correction (2026-09-28): the 0.597 reference is stale, not a clean target —
  the "bubble on/off" framing above is too narrow.** `28f04b0` is the *only* commit
  the 0.597 figure was ever measured on. Nine commits have landed since, including
  §B9 (`2efa939`, load→address gather-dependency latency — a real, RTL-verified fix
  that *adds* stall cycles, fires 57,901 times on SpMV alone) and cache/AXI
  infrastructure changes. **The entire §B6 mispredict-bubble mechanism itself
  (`flush_is_branch_mispredict`, the code this A/B toggles) is still uncommitted** —
  it exists only in the working tree, never landed. So "bubble off" does not
  reproduce the `28f04b0` baseline; it reproduces *today's committed tree minus one
  uncommitted mechanism*, which already differs from `28f04b0` for other reasons.
  Decomposing the 238,118,336 vs 230,924,306-cycle gap:
  - Everything committed since `28f04b0` (§B9 and the rest), independent of the
    bubble: **−1.8M cycles** net (229,130,211 measured with the bubble off, vs
    230,924,306 reference) — these are legitimate accuracy fixes, not regressions.
  - The uncommitted §B6 bubble alone: **+9.0M cycles** (238,118,336 − 229,130,211,
    ≈1.08 cyc/mispredict on 8.3M mispredicts, consistent with a correct 1-cycle
    bubble).
  - Net: +7.2M cycles → IPC 0.586 instead of 0.597.
  **Conclusion: comparing today's boot IPC to 0.597 compares against a moving
  target, and CVA6 RTL cannot run Linux (bare-metal co-sim only — confirmed), so
  there is currently no way to get a fresh ground-truth boot IPC to replace it.**
  Boot IPC is therefore *not independently validatable* right now; only the
  individual mechanisms that compose it (§B2, §B6, §B9, etc.) are each
  RTL-validated, on bare-metal microbenchmarks. Do not chase a specific boot-IPC
  number without a new reference point. The original open question (VP
  over-mispredicting vs. a stale baseline) is superseded by this: it was never
  purely a mispredict-count question, since the baseline itself had moved.

### B7. Memory-bound: VP over-serialises the D-cache — and the CVA6 cache was mischaracterised *(cause 1 FIXED in §B8; cause 2 OPEN)*
- **Symptom:** on a purpose-built memory-thrash benchmark (`mem_contention_bench`:
  32 KB unrolled read body > 16 KB I$, sweeping a 64 KB region > 32 KB D$, so both
  L1s miss heavily and *concurrently*), the VP runs **much slower** than the RTL:

  | mem_contention | Instr | Cycles | IPC | I$ miss | D$ miss |
  |---|---:|---:|---:|---:|---:|
  | CVA6 co-sim (RTL) | 1,638,463 | 4,334,798 | 0.378 | 332,537 | 167,093 |
  | Fast VP (CYCLE6)  | 1,638,480 | 5,699,038 | 0.288 | 405,627 | 204,804 |
  | CYCLE6_AXI (arb.) | 1,638,480 | 6,018,449 | 0.272 | 405,627 | 204,804 |

  The VP **over-counts cycles by +31%** before any contention modelling.
- **Two distinct root causes:**
  1. **Over-missing ~22% — FIXED (§B8).** Was a replacement-policy mismatch: the
     VP used LRU, CVA6 uses LFSR random. After the fix, miss counts match RTL to
     within 0.1% (I$ 332,743, D$ 166,923) and the fast-VP gap fell +31% → +18%.
  2. **The VP's memory model is too serial vs CVA6's memory-level parallelism —
     OPEN.** The blocking is on the *VP* side, not CVA6's. The VP has a single
     `dcache_miss_fu` slot, flat ~10-cyc penalty, one miss at a time, and **no
     overlap between I$ and D$ misses**. CVA6-WT overlaps: (a) stores via an
     **8-entry coalescing write buffer** (`WtDcacheWbufDepth=8`,
     `MaxOutstandingStores=7`); (b) **I$ and D$ refills are outstanding
     concurrently on the AXI** — `wt_axi_adapter.sv` carries *separate*
     `icache_rtrn_tid` / `dcache_rtrn_tid`, so their miss latencies overlap
     rather than serialise; (c) a single read MSHR + 2 load-buffer entries per
     cache. **CVA6 is NOT a blocking cache.**
- **⚠ CVA6 cache mischaracterisation (correct the docs AND the paper in revision).**
  The co-sim config `cv64a6_imafdc_sv39` selects `DcacheType = config_pkg::WT`
  (write-through). `wt_dcache_missunit.sv` has a **single** read MSHR (`mshr_q`,
  one register — not the multi-entry MSHR array of `std_nbdcache`/`hpdcache`).
  So CVA6-WT is **NOT** a "non-blocking MSHR cache that pipelines concurrent
  misses" — that describes CVA6's *other* (unused) cache options. The paper's
  §IV-D, §VI-E(3) and §VII assert non-blocking/MSHR; that wording is inaccurate
  for the WT config and should be revised to "write-through cache with an
  8-entry write buffer and a single read MSHR." (Paper submitted for initial
  review 2026-08; fold the fix into the post-review revision.)
- **AXI contention experiment (negative result).** A real matchlib `AxiArbiter`
  I$/D$ port-contention model was built as an isolated opt-in build
  (`TIMING_MODEL=CYCLE6_AXI`; vendored matchlib in `third_party/matchlib_kit`,
  builds against the VP's own SystemC 3.0.2). It is *mechanically correct*
  (arbitrates, functionally bit-identical, default build byte-for-byte unchanged)
  but moves accuracy the **wrong way**, and the reason is now confirmed at the
  mechanism level, not just empirically: **its premise is backwards.** Contention
  assumes I$ and D$ *compete* for one port and serialise; the RTL gives them
  *separate AXI transaction IDs* (`wt_axi_adapter.sv`) so they *overlap*. Real
  CVA6 has **more** memory-level parallelism than the VP, not less. Post-§B8
  (LFSR) three-way makes this stark:

  | mem_contention | Cycles | vs RTL |
  |---|---:|---:|
  | CVA6 RTL (overlaps I$/D$)      | 4,334,798 | — |
  | Fast VP LFSR (serialises)      | 5,131,111 | +18.4% |
  | CYCLE6_AXI LFSR (forces serial)| 5,408,510 | +24.8% |

  The arbiter (least parallel) is furthest from the RTL (most parallel). **Keep
  CYCLE6_AXI as an optional build; do not enable by default and do not pursue it
  for memory-bound accuracy.** The correct direction for the residual is the
  *opposite* — give the VP overlapping/outstanding misses (I$ ∥ D$ + write
  buffer), which would pull 5.13M *down* toward 4.33M.
- **Store-drain master — tested, does NOT help (measured, not assumed).** The VP
  models stores as *doubly* free: it never calls `dcache.access()` on the store
  path (so store misses aren't even counted) and `store_write_penalty=0`. That
  looked like a gap worth a store-drain master. A store-flood benchmark
  (`mem_store_bench`, 1024x-unrolled write sweep) settled it:

  | mem_store_bench | Cycles | I$ miss | D$ miss |
  |---|---:|---:|---:|
  | CVA6 RTL       | 772,390 | 67,623 | **1** |
  | Fast VP        | 975,402 | 67,679 | **3** |

  CVA6 reports **~0 D$ misses on a store flood** — its write-through **write
  buffer absorbs the stores** with no miss count and no meaningful cycle cost, so
  stores are effectively free on the RTL *too*, matching the VP. The +26% gap is
  the same I$-serialisation residual (this benchmark is I$-dominated), not stores.
  Adding a store-drain cost/master would push the VP further from the RTL — same
  wrong direction as the I$/D$ arbiter. **Do not add store-drain modelling.**
- **DMA master — cannot be validated against this co-sim.** `cva6_matchlib_system`
  has `slave_count=4` (bootrom, dram, console, halter) — **no DMA** — and the CVA6
  core RTL has no DMA engine. The VP's DMA (`inc/DMA.h` @ 0x30000000) is a
  VP-only SoC peripheral. So there is **no RTL ground truth** for DMA; a
  DMA-contention model cannot improve agreement with a reference that contains no
  DMA. **Out of scope for co-sim accuracy** (would only matter against a full SoC
  reference with DMA, which we don't have).
- **Note:** the register-only microbenchmarks never expose this — they are
  cache-resident (≈1 D$ miss), so fast VP, CYCLE6_AXI and co-sim all agree there.
  A both-caches-thrashing workload is required to see it.

### B8. Cache replacement was LRU, but CVA6 uses LFSR random — VP over-missed ~22% *(FIXED)*
- **This is the root cause of B7 cause (1)** (the ~22% miss-count gap). Resolved.
- **Root cause:** `inc/Cache.h` modelled **LRU** replacement; both CVA6 L1 caches
  use **8-bit LFSR pseudo-random** replacement (fill an invalid way first, else
  evict a random way):
  - I$: `cva6_icache.sv:389-391` — *"chose random replacement if all are valid"*,
    `repl_way = all_ways_valid ? rnd_way : inv_way`, `rnd_way` from an LFSR.
  - D$ (WT): `wt_dcache_missunit.sv:200-210` — `lfsr #(.LfsrWidth(8))`,
    `repl_way = all_ways_valid ? rnd_way : inv_way`.
- **Why it mattered:** for a sequential scan of a working set larger than the
  cache, **LRU is pathologically pessimal** — it evicts exactly the line about to
  be reused next pass, so ~100% of accesses miss. Random replacement statistically
  retains a fraction, so it misses less. The geometry was already identical
  (I$ 16 KB/4-way/16 B, D$ 32 KB/8-way/16 B), so replacement was the *only*
  difference — and it produced a systematic +22% miss bias on reuse-with-eviction.
- **Why it hid:** the validated streaming test (§VI-D of the paper) is
  compulsory-miss-only (single sweep), where LRU and random give identical counts;
  register-only microbenchmarks are cache-resident (≈0 misses). Only a
  re-swept-working-set workload (`mem_contention_bench`) exposes it.
- **Fix:** `Cache.h` now fills the first invalid way, else evicts
  `lfsr % WAYS` and advances an 8-bit Fibonacci LFSR (taps x^8+x^6+x^5+x^4+1),
  one LFSR shared across sets — mirroring CVA6's single-LFSR scheme and its
  `update_lfsr = cache_wren & all_ways_valid` timing. `lru_time` removed.
- **Result — miss counts now match RTL to within 0.1%:**

  | mem_contention | I$ miss | D$ miss | Cycles | IPC |
  |---|---:|---:|---:|---:|
  | CVA6 RTL          | 332,537 | 167,093 | 4,334,798 | 0.378 |
  | VP LRU (before)   | 405,627 | 204,804 | 5,699,038 | 0.288 |
  | **VP LFSR (after)** | **332,743** | **166,923** | **5,131,111** | **0.319** |

  I$ +0.06%, D$ −0.10% (were +22% / +23%). The cycle gap to RTL shrank from
  **+31.5% → +18.4%**; the residual is now attributable to B7 cause (2) (the VP's
  fully-blocking D-cache vs CVA6-WT's 8-entry write buffer + single read MSHR),
  no longer confounded by miss counts.
- **Zero regression:** all validated microbenchmarks bit-identical after the swap
  (int_alu 2,400,256; int_nobranch 3,081,167; int_indep 2,249,992; robust_stress
  4,442,265) — their misses are cold/compulsory, hence replacement-independent.
- **⚠ Not bit-exact vs RTL by construction:** matching CVA6's exact miss *sequence*
  would need its precise LFSR seed/polynomial and per-cycle update timing. This
  removes the systematic LRU bias and lands within ~0.1% on counts; small
  per-workload differences from LFSR phase are expected and acceptable.
- **Paper implication (revision):** the SpMV 9.3% residual, attributed in §VI-E(3)
  to the blocking D-cache, is likely **substantially a replacement-policy artifact**
  (LRU vs random) — SpMV's irregular gather has heavy reuse-with-eviction. Re-run
  SpMV with this fix before committing to the "MSHR layer is highest priority"
  framing; the cheap correct fix (this) may absorb most of it.
- **SpMV verification (integer variant, so the co-sim runs FP-free):** with §B8,
  the VP's D$ miss count on the SpMV gather matches the RTL to **0.17%** (VP 7,777
  vs RTL 7,764). Confirms the SpMV *memory* divergence was replacement policy, now
  resolved. (A residual *cycle* gap remains — see §B9 — from a different, non-cache
  cause.)

### B9. Load->load-address (gather) dependency under-charged by ~2 cycles *(FIXED)*
- **Symptom:** on gather/pointer-chase code the VP runs **too fast**. Integer SpMV
  (same gather as the paper's kernel): VP 1,094,790 cyc vs RTL 1,262,614 (**−13%**),
  even though D$ misses and instruction counts match (see §B8 verification).
- **Isolated with a pure pointer-chase** (`ptr_chase_bench`: `idx = arr[idx];`, a
  load whose *address* is the previous load's result; 4 KB array so all D$ hits —
  no miss noise, no MUL/FP):

  | ptr_chase | Instr | Cycles | D$ miss | cyc/iter |
  |---|---:|---:|---:|---:|
  | CVA6 RTL       | 2,002,600 | 3,205,274 | 256 | 8.01 |
  | Fast VP (pen=1)| 2,002,617 | 2,405,099 | 257 | 6.01 |

  Instr and D$ misses match, so the entire gap is dependency latency:
  **RTL charges exactly +2.0 cycles per dependent-load-address vs the VP.**
- **Root cause:** the VP charges `load_hit_penalty=1` for *any* load-use. But CVA6's
  latency for a load whose result routes to **address generation of a following
  load/store** (gather / pointer chase) is ~3 cycles, i.e. **~2 cycles more than a
  load->ALU use**. The VP models both the same.
- **Why it hid & why a global bump is WRONG:** `long_test3` (load->ALU) matches the
  RTL exactly at `load_hit_penalty=1` (§B5). Setting the global penalty to 2 fixes
  SpMV (−0.9%) but **breaks long_test3** (+18%). So the fix must be **targeted to
  the load->address case only**, not all load-uses. Register-only microbenchmarks
  have no dependent-load-address chains, so they never exposed it.
- **Fix (implemented, `load_addr_penalty=2`):** a per-register "derives from a
  recent load" **taint token**. A load taints its rd (at completion); address
  arithmetic (add/shift/OP-IMM/OP) forwards the taint to its rd and **consumes
  the source token** (so a value loaded once and reused — e.g. a PIC/GOT base
  pointer — cannot keep re-tainting addresses); a load/store whose base (rs1) is
  tainted pays a `load_addr_penalty`-cycle bubble (detection cycle + countdown).
  Set `load_addr_penalty=0` to disable. New stat: `load_addr_events`.
- **Verified (fast VP vs co-sim):**

  | | RTL | VP+B9 | err | B9 fires |
  |---|---:|---:|---:|---:|
  | ptr_chase (isolated cause) | 3,205,274 | 3,205,097 | **−0.006%** | 399,999 |
  | int SpMV (motivating case) | 1,262,614 | 1,200,581 | **−4.9%** (was −13%) | 57,901 |
  | long_test3 (control, load->ALU) | (1,100,602) | 1,100,218 | unchanged | 0 |
  | int_alu / int_nobranch (controls) | — | unchanged | — | 0 |
  | mem_contention (no gather) | — | +67 cyc | negligible | 199 |

  ptr_chase (the pure isolation) matches to 0.006%; SpMV −13%→−4.9% (within the
  paper's 5.1% worst case); all load->ALU and register controls bit-identical
  (0 false fires). A first cut without the token-consume step spuriously fired
  204k times on mem_contention (a GOT base pointer) for +95k cycles — the
  single-use token reduced that to 199 fires / +67 cycles.
- **Scope note:** this is a *pipeline/timing* divergence, entirely separate from the
  cache/replacement issue (§B8). The two SpMV causes are now decomposed: memory =
  §B8 (fixed), cycles = §B9 (this). For the paper's revision, the SpMV residual is
  **replacement policy + gather-load latency**, not the "blocking D-cache."

### B11. D$ hit latency: RTL is 3 cycles (pipelined), VP modelled 2 *(MEASURED; fix is opt-in `VP_LOAD_LAT=2`)*
- **Note on numbering:** this was originally drafted as "§B10," but §B10 is already
  claimed elsewhere — the README and stale code comments (`icache_miss_penalty`,
  `axi_slave_latency` in `inc/CPU_P64_6_Cycle.h`) reference "§B10" for a *different*,
  older story (I$ miss penalty 10→7 / AXI slave-latency recalibration) that was
  never actually written up as a section body here. Renumbered to §B11 to resolve
  the collision without overwriting that pre-existing (if dangling) reference. If
  someone writes up the real I$-penalty/AXI-recalibration derivation later, it
  belongs at §B10, not here — this entry is about D$ *hit* latency only.
- **Symptom / question raised:** CVA6's own design doc (`cva6/docs/03_cva6_design/intro.rst:11`)
  states *"the instruction RAM ... has an access latency of 1 cycle on a hit, while
  accesses to the data RAM (or L1 data cache) have a longer latency of 3 cycles on a
  hit."* The VP models `load_hit_penalty{1}` (§B5) — a literal 3x gap from the
  documented RTL figure, never previously checked against this specific doc line.
- **I$ side: checked, no discrepancy.** Traced `cva6_icache.sv`'s FSM directly:
  `IDLE` (cycle N, request accepted) -> `READ` (cycle N+1, `dreq_o.valid=1` on a
  hit) = exactly 1 cycle, matching the doc. The VP's own IF->ID pipeline register
  (`fetch_id_next` -> `fetch_id_reg`, `CPU_P64_6_Cycle.cpp:264-297`) already provides
  that same 1-cycle transit for every instruction regardless of hit/miss — so the
  VP's "0 extra stall cycles" *is* the correct way to express the RTL's "1 cycle
  latency," not a gap.
- **D$ side: RTL trace only accounts for 1 of the 3 documented cycles.** Traced
  `cache_ctrl.sv` (`IDLE`->`WAIT_TAG`, `data_rvalid` fires same cycle as tag hit)
  and `load_unit.sv` (`valid_o` asserted combinationally the same cycle as
  `req_port_i.data_rvalid`, `load_unit.sv:433-435`) — together these show only
  **1 cycle** from request to valid result, not 3. The other 2 cycles the doc
  claims were not traced (candidates: DTLB/MMU translation latency, or an
  issue-stage/scoreboard forwarding boundary) — **not yet resolved.**
- **Empirical test (this session, `mem_contention_bench` vs real CVA6 co-sim
  target of 4,334,798 cycles):**

  | `load_hit_penalty` | VP Cycles | Error |
  |---|---:|---:|
  | 1 (current) | 4,266,611 | −1.57% |
  | 2 | 4,308,527 | **−0.61%** (closest) |
  | 3 (literal RTL doc figure) | 4,370,231 | +0.82% (overshoots) |

  Direction confirmed correct (raising the penalty closes the gap), but the doc's
  literal 3-cycle figure overshoots; 2 is the closest integer on this one
  benchmark. **Not adopted** — `load_hit_penalty` left at 1 in the working tree,
  this was a temporary probe (reverted, confirmed via `git diff`).
- **Why 3 plausibly overshoots — likely the same root cause as §B5/§B9's
  "blocking slot" finding.** The RTL doc itself says the scoreboard is meant to
  *"hide latency to the data RAM by issuing data-independent instructions"*
  (`intro.rst:10`) — real CVA6 likely only pays the full 3-cycle latency on loads
  whose result is needed immediately, and overlaps the rest. The VP's
  `load_hit_fu` is a **single blocking slot** (§B5's own note: "two independent
  loads cannot overlap... it simply stopped being observable once the penalty
  dropped to 1 cycle, because a 1-cycle blocking unit and a 1-cycle scoreboard
  dependency are indistinguishable in cycle count"). Raising the constant to 3
  makes that blocking fully observable — the VP force-serializes *every* D$ hit
  for the full 3 cycles, including ones real CVA6 would have hidden, which is
  consistent with the over-count. §B9 already demonstrated the same class of
  effect for the address-dependency case: a global penalty bump fixes one
  benchmark and breaks another (`long_test3` +18%), because the real fix is
  giving the VP overlap capability, not a bigger flat constant.
- **Status: OPEN.** Not fixed, not adopted. Needs: (a) trace the DTLB/MMU and
  issue-stage forwarding paths to find the RTL's other 2 cycles; (b) re-test any
  candidate value against `long_test3` too (the benchmark that originally set this
  constant to 1, §B5) before considering a global penalty change, given §B9's
  precedent that global bumps fix one benchmark while breaking another.

---

## C. Measurement caveats discovered

- **Instruction-count difference (VP vs CVA6):** a fixed **~18–19 instruction**
  offset — CVA6 reports the `minstret` value read *inside* the program (before the
  halter writes); the VP counts to its actual halt (after the readout epilogue).
  Not an execution difference. Earlier scattered deltas (+14/+46/+125) were
  `--max-instr` overshoot; adding an **HTIF halt** to every benchmark makes the
  offset a constant ~18–19.
- **The counter window differs, so epilogue traffic shows up only in the VP.**
  Each benchmark samples its counters with `csrr` *inside* the program, then runs a
  readout epilogue (halter stores + the HTIF halt store). CVA6's counters are frozen
  at the `csrr`; the VP keeps counting to its actual halt. This produces the constant
  ~18-20 instruction offset **and** small phantom D$ deltas: `int_alu` is
  register-only, so CVA6 reports **0** D$ accesses while the VP reports **1** —
  epilogue memory traffic CVA6 was never alive to see. Not a modelling difference.
  (Contrast `fp_bench`, whose `fld`s execute *before* the `csrr` and so are counted
  by both — once §A2 was fixed.)
- **HPM "access" counters are not directly comparable.** CVA6 I$/D$ *access* events
  count fetch/port *activity* (speculative, multi-port OR), so they exceed retired
  instructions and differ from the VP's exact software counters. Compare **miss
  counts** and **loads/stores**, not the "access" denominators.
- **The co-sim has no main-memory model at all — despite the name `dram`.** The
  instance at `0x80000000–0x8FFFFFFF` (`cva6_matchlib_system.h:41,87`) is a
  `SlaveFromFile<axi::cfg::standard>`, a matchlib **testbench** class whose storage
  is a `std::map<Addr,Data>` (`axi/testbench/SlaveFromFile.h:65`). It has **no
  latency, wait-state or delay parameter**; its only `wait()` calls are AXI
  handshake ticks. From the core it behaves as an idealized zero-wait-state SRAM.
  It is **not DRAM**, and the variable name is misleading.
  - Do not confuse this with the `tc_sram_wrapper` modules seen when grepping for
    "sram" — those are the tag/data arrays **inside** CVA6's L1 I$/D$, not memory.
  - Consequence: co-sim cache misses are nearly free. The VP by contrast applies a
    flat 107-cycle miss penalty (`icache/dcache_miss_penalty`), which is why the VP
    is *pessimistic* on miss-heavy code — and why §B5's memory constants are fitted
    to a testbench stub rather than to hardware.
- **Branch counts differ by definition** (CVA6 counts jumps/calls/returns as
  branches; the VP does not) — compare mispredict *rate*, not raw counts.

---

## D. Accuracy summary

All rows below were re-measured in a single session against the **same `-O1` ELFs**
on both simulators. Instruction counts differ by a constant 17–20 (readout epilogue,
see §C).

| Unit tested | Benchmark | CVA6 IPC | VP IPC | IPC err | Status |
|---|---|---:|---:|---:|---|
| Integer ALU (looped) | int_alu | 0.917 | 0.917 | −0.0% | ✅ B2 |
| Integer ALU (dependency chain, branch-free) | int_nobranch | 0.833 | 0.833 | −0.0% | ✅ B4 |
| Integer ALU (independent ops, branch-free) | int_indep | 0.998 | 0.998 | −0.0% | ✅ control |
| Integer divide | div_stress | 0.165 | 0.165 | +0.0% | ✅ B1 |
| Load/store + MUL | long_test3 | 0.727 | 0.727 | −0.0% | ✅ B5 |
| Branch mispredict recovery | robust_stress | 0.685 | 0.685 | **~0.0%** | ✅ **B6 fixed** |
| FP add/mul | fp_bench | 0.500 | 0.518 | +3.6% | ✅ B3 |
| FP add/mul/div/sqrt | fp_stress | 0.145 | 0.153 | +5.1% | ✅ B3 |

**Six of eight benchmarks now agree with the RTL to within 0.05%; the worst
residual is +5.1%, and both remaining residuals are on the FP path.**

The remaining residuals are understood:
- **fp_bench (+3.6%), fp_stress (+5.1%)** — the FPnew latency model is approximate
  (ADDMUL depth 4, DIV 20, SQRT 22 are fitted, not derived from the RTL). No
  isolated per-FP-op latency experiment has been run.

### Raw cycle/instruction data (for reproduction)
| Benchmark | CVA6 instr | CVA6 cycles | VP instr | VP cycles |
|---|---:|---:|---:|---:|
| int_alu | 2,200,041 | 2,400,417 | 2,200,061 | 2,400,216 |
| int_nobranch | 2,566,039 | 3,080,384 | 2,566,059 | 3,080,175 |
| int_indep | 2,245,043 | 2,249,473 | 2,245,066 | 2,249,280 |
| div_stress | 186,659 | 1,130,126 | 186,678 | 1,129,928 |
| long_test3 | 800,038 | 1,100,413 | 800,056 | 1,100,186 |
| robust_stress | 3,127,046 | 4,567,925 | 3,127,063 | 4,567,716 |
| fp_bench | 1,300,059 | 2,600,478 | 1,300,077 | 2,509,625 |
| fp_stress | 900,047 | 6,200,440 | 900,065 | 5,900,208 |

All eight re-measured fresh post-§B6-fix (`build_cycle6_axi`, current HEAD).

### Benchmark design notes
- `int_nobranch` / `int_indep` are **1024×-unrolled** so the loop branch is <0.1% of
  instructions, while the ~4–5 KB code footprint stays inside the 16 KB I-cache
  (I$ misses ≈ code_bytes ÷ line_size: CVA6 324×16 B ≈ VP 85×64 B ≈ 5.3 KB ✓ — an
  independent confirmation that both cache models are correct).
- `div_stress` still contains one data-dependent branch (`if (a<0) a = -a;`),
  producing ~4,000 mispredicts. Harmless (division dominates ~83% of cycles; VP
  3,999 vs CVA6 3,995) but it is the one impurity in the arithmetic set.

### Remaining known gaps
| Workload | CVA6 | VP | Error | Cause |
|---|---:|---:|---:|---|
| Tiny (cam_bench) | 0.682 | 0.195 | −71% | too small — cold-start dominated (not a valid timing measurement) |

**robust_stress was previously listed here at +16% error, cause "not established."**
That figure is now known to be **stale** — it was superseded by the more careful,
same-session re-measurement in the accuracy-summary table above (+2.8%, later
traced and fixed as §B6). Two things were wrong with the old +16% entry, not just
its number: it also carried a retracted "lockstep frontend" explanation, which was
wrong on two counts — (1) the VP *does* model the 4-entry decoupled instruction
queue (`fetch_queue`, `CPU_P64_6_Cycle.cpp:166`), and (2) the sign was backwards
(a missing queue would make the VP pessimistic, not optimistic). See §B6 for the
real root cause (a missing 1-cycle frontend-redirect bubble on the flush path) and
its fix, verified down to ~0.0% error.

- **RESOLVED by measurement (2026-10-02, `tests/fpmb/fpmb_l*`):** six load-use microbenchmarks, same ELF on VP
  and RTL (10,000 iterations): CVA6's D$ hit→use latency is **3 cycles, pipelined** (one load per cycle).
  Load→use at distance 1 costs 2 stall cycles, at distance 2 costs 1, at distance 3 none. The VP's
  `load_hit_penalty=1` gave distance 1 → 1 stall, distance 2 → 0.

  | Test (cycles) | RTL | VP latency 1 (old) | VP latency 2 |
  |---|---:|---:|---:|
  | `ld; add` (distance 1) | 660,404 | 500,117 | **660,102** |
  | `ld; x; add` (distance 2) | 420,424 | 330,135 | **420,121** |
  | `ld; ld; bne` | 420,408 | 420,103 | **420,104** |
  | `ld; sd` (store data) | 660,405 | 500,117 | **660,102** |
  | `lw; addw` | 660,404 | 500,117 | **660,102** |
  | `lw; lw; slt; add` | 500,422 | 500,117 | **500,117** |

  Why the earlier global-penalty test failed (it "broke long_test3"): `long_test3` matched at latency 1 only
  through cancelling errors, a missing +1 same-register stall (§B12) against a too-short load latency. With
  both corrected it matches again (1,100,117 vs RTL 1,100,413).
- **Implementation:** the load-hit unit is now a small pipeline (`load_hit_pipe`, slots = latency) so loads
  can issue every cycle; a plain penalty of 2 on the old single-slot unit would add a structural stall the
  RTL does not have. Opt-in: `VP_LOAD_LAT=2`. Default (1) is bit-identical to before.

### B12. Same-destination (WAW) issue stall: RTL waits for the earlier writer to commit; the VP's "shift bubble" is a misattribution *(PARTIAL — opt-in, not default)*
- **Found via:** `tests/fpmb/` microbenchmarks (same ELF on VP and RTL co-sim). A pure chain of dependent
  `addi a5,a5,1` runs at RTL CPI 1.91 vs VP 1.03; this drove the FP-benchmark gap investigation (§B3 addendum).
- **RTL mechanism (read, `cva6/core/issue_read_operands.sv` `gen_check_waw_dependencies`):** issue stalls while
  ANY in-flight (issued, uncommitted) scoreboard entry has the same rd (`rd_clobber_gpr_i[rd] != NONE`), and is
  released in the cycle the commit stage writes that register. Back-to-back ALU pair = +1 (waits for commit);
  distance 2 = already committed = free; long-latency producers (mul/div/load) make the consumer wait for their commit
  (`ld a0; add a0,..` ≈ 6 cyc/pair). RAW into a *different* rd, WAR, distance-2 WAW: free.
- **Measured (RTL, ±0.04%):** 32-op chains `add/xor/addi/addw` same-rd 2.0 cyc/op; shift→different-rd free
  (VP shift rule charged it, +47%); `int_nobranch`'s shift→add "bubble" was same-rd WAW all along.
- **VP experiments (env `VP_WAW_BUBBLE`, default 0 = old shift rule):**
  1 = EX-stage same-rd approximation (+ multiply-completion marker); 2 = faithful `scoreboard.rd_clobber[rd].busy` check.
  Mode 2 matches every integer microbenchmark, `robust_stress` (4,567,713 vs RTL 4,567,925), `int_alu`,
  `int_nobranch`, `int_indep`, `multiply_bare` (+0.01%). **Fails:** `long_test3` +9%, `div_stress` +4.4%.
- **Why it still fails (bisected with `fpmb_wa/wb/wc_lt3_*`):** the WAW wait overlaps other stalls in RTL
  (addw→addw same-rd after loads is free: RTL 10 vs mode 2 11), and the VP's multiply→consumer after a load wait is
  1 cycle slower than RTL (`mulw a5..; addw a6,a5,..`: RTL 10 vs VP 11). In the old model that VP error cancels the
  missing WAW +1 in `long_test3`, so the exact match there is a compensation, not correctness. VP already commits
  2/cycle like RTL, so commit width is not the difference; needs RTL waveforms of commit/wt_valid timing.
- **Rejected:** blanket "+1 on any adjacent dependent op" (robust_stress +9.6%, int_alu +16.7%).
- **Open:** mul→consumer latency after load wait; WAW/RAW stall overlap; load→same-rd stall (mode 2 gives 810k vs
  RTL 970k on `fpmb_qd_load_waw`); div/rem commit timing (div_stress).

### B13. RTL co-sim memory model misplaced narrow stores; CoreMark validated against RTL; VP branch predictor aligned to CVA6 *(co-sim FIXED separately; predictor FIXED opt-in)*
- **Co-sim bug (testbench, not CVA6):** matchlib's `axi/testbench/SlaveFromFile.h` wrote byte lane *k*
  of a write beat to `transfer_address + k` instead of `beat_aligned_address + k`. A narrow store at
  byte offset *n* landed at *2n*: 8/16/32-bit stores at non-zero offsets corrupted memory; offset-0 and
  64-bit stores were fine. Found with `tests/coremark/strobe_probe.c` (RTL: 8-bit at +1 landed at +2,
  16-bit at +2 at +4, 32-bit at +4 at +8; VP correct). This is the "sub-word write strobe" limitation
  the DVCon paper §VI-A describes, but it is a misplacement and it also hits 32-bit stores.
- **Why earlier RTL numbers were still right:** a store that hits the write-through D$ updates the cache
  correctly; only the memory copy is wrong, so it is only visible on a later refill. All eight paper
  microbenchmarks, `spmv_int_bench` and `fp_*` give **identical** RTL cycles/instructions on the fixed
  co-sim. Programs that write fresh memory narrowly and read it back (CoreMark, median) were corrupted.
- **Fix:** local override `systemc/tb_override/axi/testbench/SlaveFromFile.h` (vendored submodule left
  untouched), built into `systemc/build-system-strobefix/` (testbench objects only; Verilated CVA6 reused).
- **CoreMark (2K performance run, `tests/coremark/`, same ELF, CRCs validated on both):**

  | | RTL (fixed co-sim) | VP | VP error |
  |---|---:|---:|---:|
  | Instructions, 10 iter | 3,625,993 | 3,625,993 | identical |
  | Cycles, 10 iter | 5,121,158 | 5,215,705 | **+1.85%** |
  | Cycles, 30 iter | 15,361,670 | 15,645,957 | **+1.85%** |

  Not an official CoreMark score (no 10 s rule, no console); a cycle-count comparison. (Old default VP.
  The VP-side causes of this residual are broken down below.)
- **Also now on RTL:** `median` 4,159,515 cyc (VP 4,337,086, +4.3%); Dhrystone rebuilt with the shared
  `crt0.S`/`link.ld` (`tests/dhrystone/`; the old build's entry was 0x8000033c, so the co-sim bootrom
  jumped into the middle of the code): RTL 1,113,360 cyc vs VP 1,162,761 (**+4.4%**), instr 884,530 vs 884,519.
- **VP branch predictor did not follow CVA6's rules (sizes were already matched) — FIXED, opt-in `VP_BP=cva6`.**
  BTB 32 / BHT 128 / RAS 2 matched the RTL config, but the rules differed (read from
  `cva6/core/frontend/{frontend,bht,btb,ras,instr_scan}.sv`): CVA6 predicts `jal` always taken from the decoded
  immediate (the VP needed a BTB hit, so every first-time or aliased `jal` was a full mispredict); branches use a
  BHT with a valid bit and a backward-taken static fallback, indexed by PC[7:1] (VP: PC[8:2]); returns pop a
  2-entry RAS that is never flushed (VP cleared it on every mispredict); the BTB (tagless, PC[5:1]) is written
  only by a mispredicted non-return `jalr`. **Bug found while checking the first version:** BHT counters reset to
  `00` (`bht_q <= '0`); the `2'b10` in the flush branch is unreachable because the frontend ties `flush_bp_i`
  to 0. Starting at 2 made Dhrystone's N,T loop branch oscillate (2 mispredicts/iteration instead of 1).

  | Mispredicts (RTL timed region / VP old / VP `cva6`) | RTL | VP old | VP `cva6` |
  |---|---:|---:|---:|
  | CoreMark, 10 iterations | 77,213 | 122,836 | 77,138 (−0.1%) |
  | `median` | 269,509 | 270,011 | **269,509** |
  | Dhrystone | 6,015 | 22,037 | 6,026 |
  | `robust_stress` | 125,498 | 125,500 | **125,498** |
  | `div_stress` | 3,995 | 3,999 | 3,996 |
  | `multiply` | 65,411 | 65,416 | 65,412 |

  Six benchmarks that matched before are unchanged within ±10 cycles. Diagnostics: stats print mispredicts by
  kind; `VP_MP_TRACE=<file>` dumps every mispredict PC.
- **Combined configuration** (`VP_BP=cva6 VP_LOAD_LAT=2 VP_WAW_BUBBLE=1`), cycles vs RTL:

  | | RTL | old default | combined |
  |---|---:|---:|---:|
  | CoreMark (10 it.) | 5,121,158 | +1.85% | **+2.4%** |
  | `median` | 4,159,515 | +4.3% | **+1.2%** |
  | Dhrystone | 1,113,360 | +4.4% | **−1.7%** |
  | `spmv_int` | 1,262,614 | −5.3% | **−0.4%** |
  | `mem_contention` | 4,334,798 | −1.6% | **+0.1%** |
  | `ptr_chase` | 3,205,274 | −0.02% | **−0.02%** |
  | `long_test3` | 1,100,413 | −0.02% | **−0.03%** |
  | `div_stress` | 1,130,126 | −0.02% | +1.75% |
  | `robust_stress`, `int_*`, `multiply` | | ≈0% | ≈0% |

  The old CoreMark +1.85% was too many mispredicts cancelling a too-short load latency. Faithful WAW mode 2 is
  *worse* with loads (CoreMark +8%, SpMV +8%, `ptr_chase` +25%): use mode 1. Still open: `div_stress` +1.75%
  (WAW pairs around div/rem), CoreMark +2.4%, FP gap (§B3).
- **OPEN:** `samples/spmv_bare.csv` (double SpMV) hangs on the RTL; its CSV and the VP hex are from
  different builds. Rebuild both from one ELF with the shared crt0 before using it.

### B14. Linux boot with the CVA6-aligned predictor *(2026-10-02)*
- **Hang found and fixed (VP_BP=cva6 only).** `c.ebreak` (kernel `BUG()` after `c.beqz`) is scanned by
  CVA6's `instr_scan` like `c.jalr`, so it can be predicted taken through a stale tagless-BTB target. The
  VP's ID stage then flushed for "non-branch predicted taken", which also wiped the older branch that Issue
  had dispatched the same cycle, orphaning its ROB entry (deadlock at boot cycle ~5.2M). CVA6 has no such
  decode-stage check (`is_ctrl_flow_i` is unused in `issue_stage.sv`); the older branch's EX resolution
  flushes the wrong path. Under `VP_BP=cva6` the ID check is now skipped.
- **Also fixed:** instruction-fetch faults (page fault / PMP) were taken at Fetch even on a wrong path;
  they are now deferred until the front end has drained (`fetch_fault_nonspeculative()`).
- **Known, not yet fixed:** `cpu_process_IRQ()` does not clear `load_hit_pipe`.
- **Boot results** (same kernel/initramfs, to "Starting interactive shell"):

  | Config | Instr | Cycles | IPC | Mispredicts | Predict rate |
  |---|---:|---:|---:|---:|---:|
  | old predictor (default) | 139,613,943 | 238,118,336 | 0.586 | 8,312,109 | 56.3% |
  | `VP_BP=cva6` | 138,836,464 | 219,757,767 | 0.632 | 4,271,814 | 77.3% |
  | `VP_BP=cva6 VP_LOAD_LAT=2 VP_WAW_BUBBLE=1` | 139,008,681 | 221,096,791 | 0.629 | 4,295,799 | 77.2% |

  The 56% predict rate questioned in §B6 was the VP predictor, not the workload: CVA6's rules halve the
  boot's mispredicts. There is still no RTL reference for Linux boot. Diagnostics: `VP_WATCHDOG=<cycles>`,
  `VP_BP_MASK`, `VP_MP_TRACE=<file>`.

### B15. D$ miss fill tail, divider timing, and the frozen default configuration *(2026-10-02)*
- **D$ miss cost (tests/dmiss/, same ELF, 20,000 misses over a 256 KB region):** the RTL charges about 4
  cycles more per isolated miss than the AXI-model VP (`dmiss_d16` RTL 360,035 vs VP 280,036). Back-to-back
  independent misses cost about 3 more (`dmiss_i16`), so the extra cycles are a fill tail after the refill
  that the next miss partly overlaps. Modelled as `dmiss_extra=5` (result usable 4 cycles after the refill;
  the tail pipe's own cycle makes it 5) and `dmiss_block=3` (miss path stays occupied 3 of them). The miss
  slot then frees while the result is still in `dmiss_tail[]`. Results: `dmiss_d16` 0.0%, `dmiss_i16`
  +2.1%, double SpMV −9.6% → **−0.1%**. Not matched: a hit to a line just refilled (`dmiss_d8` −6.9%), and
  `mem_contention` (+0.1% → +5.1%): its earlier match relied on `axi_slave_latency` being tuned on it while
  the VP still serialises I$ and D$ misses (§B7 cause 2).
- **Divider (tests/fpmb/fpmb_dv_*, 8,000 divides, cycles per li/li/div/add group):**

  | | RTL | VP |
  |---|---:|---:|
  | result unused, next `div` same rd | 35.5 | 33.0 |
  | result used immediately | 35.5 | 37.5 |
  | `div` then `rem` (other rd) | 64.5 | 66.0 |
  | trivial divide (|a|<|b|) | 7.6 | 10.5 |

  The RTL makes a same-rd divide wait for the previous one to commit (+2.5) but overlaps the result use with
  that wait; the VP charges the use and not the WAW wait, and over-charges trivial divides by ~3 cycles.
  `div_stress`'s old −0.02% was these cancelling; with the ALU WAW rule (mode 1, which the RTL confirms for
  and/ori/addi/or pairs: 2 cycles/op) it is +1.74%. Left as a known limit.
- **Frozen defaults** (AXI build; old behaviour: `VP_BP=legacy VP_LOAD_LAT=1 VP_WAW_BUBBLE=0 VP_DMISS_EXTRA=0`):
  CVA6 predictor rules, D$-hit latency 2, same-register stall mode 1, D$ fill tail 5/3. Timed-region cycles:

  | Benchmark | RTL | VP | Err |
  |---|---:|---:|---:|
  | int_alu | 2,400,417 | 2,400,130 | −0.01% |
  | int_nobranch | 3,080,384 | 3,080,096 | −0.01% |
  | int_indep | 2,249,473 | 2,249,183 | −0.01% |
  | div_stress | 1,130,126 | 1,149,840 | +1.74% |
  | long_test3 | 1,100,413 | 1,100,124 | −0.03% |
  | robust_stress | 4,567,925 | 4,567,638 | −0.01% |
  | fp_bench | 2,600,478 | 2,509,560 | −3.50% |
  | fp_stress | 6,200,440 | 5,900,141 | −4.84% |
  | multiply | 3,279,837 | 3,279,077 | −0.02% |
  | median | 4,159,515 | 4,209,243 | +1.20% |
  | Dhrystone (2000 runs) | 1,113,360 | 1,094,827 | −1.66% |
  | CoreMark (10 / 30 iter.) | 5,121,158 / 15,361,670 | 5,243,331 / 15,728,358 | +2.39% |
  | SpMV double (`tests/spmv/`) | 99,142 | 99,017 | −0.13% |
  | SpMV integer | 1,262,614 | 1,287,672 | +1.98% |
  | ptr_chase | 3,205,274 | 3,205,500 | +0.01% |
  | mem_contention | 4,334,798 | 4,554,375 | +5.07% |

  RTL = `systemc/build-system-strobefix/Vcva6`; `mem_contention`, `ptr_chase`, `spmv_int` and all eight
  microbenchmarks give identical RTL numbers on the old and fixed co-sim. Mispredict counts match within 0.7%
  everywhere (exactly on robust_stress, median). Also fixed: interrupt entry now clears `load_hit_pipe` and
  `dmiss_tail` (stale results could complete a reallocated ROB entry).
- **Linux boot on the frozen defaults:** 136,596,724 instr / 222,179,066 cycles, **IPC 0.615**,
  4,272,919 mispredicts (76.5%), I$ 4.22M / D$ 1.29M misses, 426 s wall-clock (host shared).

### B16. FP timing aligned to CVA6 RTL; locality and cache-sweep benchmarks on RTL *(2026-10-02)*
- **What the old "FPnew latency" gap really was.** The dependent-chain microbenchmarks (`fadd.d a,a,b`) read
  *and write* the same register, so they measure latency **plus** CVA6's same-destination commit wait (as for
  integers, §B12). Proof (`tests/fpmb/fpmb_fq_*`): a same-register FP op with no data dependency
  (`fq_fadd_wawonly`) costs 5 cycles/op on RTL; alternating-register chains cost less than same-register ones.
  Raising latencies to the chain cost (5) fixed the microbenchmarks but overshot `fp_bench`/`fp_stress` by
  +12-13%, which is why the earlier flat re-fit "overshot the aggregates".
- **Model now:** (1) an op whose rd (int or FP) is still the destination of an uncommitted FPU op waits for
  that commit (`VP_FPU_WAW=0` disables); (2) true latencies = RTL chain cost − 1: `fadd/fsub/fmul/fmadd.d` 4,
  `fadd/fsub/fmul.s` 3, `fmadd.s` 4, `fdiv/fsqrt.d` 23, `fdiv/fsqrt.s` 13, `fsgnj/fmin/fmax` 3, compares 3,
  FP→int converts 4, `fmv.x` 2; (3) `fpu_wrap.sv` behaviour: add/mul ops keep issuing while div/sqrt runs
  (the VP used to stall every FP op), and a div/sqrt behind a busy div/sqrt waits in a one-entry hold
  register, starting one cycle before the previous result leaves (`VP_FPU_LEGACY_BLOCK=1` = old).
- **Results** (RTL / VP): all chain tests (add, sub, mul, fmadd, div, sqrt; .d and .s), sign-inject, min, moves
  both ways, `fsd` after `fadd`, alternating-register and WAW-only tests within 0.3%. Residuals: compare→int
  use +4.6%, convert→int use +3.9%, FP load→use −4.9%, back-to-back sqrt/div +2.2% (RTL lies between two
  whole-cycle values). **fp_bench −3.50% → +0.35%; fp_stress −4.84% → +3.22%.** No other benchmark moved.
- **Memory benchmarks newly on RTL** (`tests/mem/`, shared crt0, same ELF): `cache_sweep_bench` RTL 10,283,200
  vs VP 10,163,638 (−1.16%); `locality_bench` RTL 7,439,113 vs VP 6,898,872 (**−7.26%**) with identical D$
  misses (262,757 / 262,705). Same open mechanism as `dmiss_d8` (§B15): the RTL charges about 2 more cycles
  per miss when misses are interleaved with hits.

### B17. `median` was compared across two different binaries *(2026-10-05)*
- `systemc/samples/median_bare_vp.hex` (VP) and `median_bare.csv` (RTL) are **different builds**; all other
  benchmarks were checked to be the same ELF (the eight microbenchmarks, multiply, SpMV integer, ptr_chase,
  mem_contention, and everything under `tests/`). The RTL ELF cannot run on the VP as-is: its data sits on the
  VP's HTIF stop address 0x80001000.
- Rebuilt with the shared crt0/link.ld (`tests/median/`), same ELF on both sides, results identical (result
  0x771153e19c617790): RTL 3,774,612 cycles / 2,076,365 instr (IPC 0.550), VP 4,030,227 / 2,076,354 (IPC 0.515):
  **cycles +6.77%, IPC −6.34%**. Mispredicts 269,506 vs 269,508; D$ misses 301 RTL. The earlier "+1.2%" was not
  a valid comparison. Cause not yet isolated (branch counts match, so a pipeline effect).
