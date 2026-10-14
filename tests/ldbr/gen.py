#!/usr/bin/env python3
# Load-fed vs ALU-fed mispredicted branches: four probes, same ELF on the VP and the CVA6 RTL.
#   A: lw; beqz              (branch uses the load result directly)
#   B: lw; andi; beqz        (load result goes through an ALU op first)
#   C: lw; 2 independent addi; beqz   (load result is ready by the time the branch issues)
#   D: as A, but the table is all ones, so nothing mispredicts (control)
N = 64          # unrolled branch sites, one table entry each
ITER = 900      # window slides one entry per outer iteration: TBL >= N + ITER
TBL = 1024

def kernel(v):
    out = []
    for k in range(N):
        out.append(f"lw t0, {4*k}(a0)")
        if v in "AD":
            out.append("beqz t0, 1f")
        elif v == "B":
            out.append("andi t1, t0, 1"); out.append("beqz t1, 1f")
        elif v == "C":
            out.append("addi a3, a3, 1"); out.append("addi a4, a4, 1"); out.append("beqz t0, 1f")
        out.append("addi a2, a2, 1")
        out.append("1:")
    return "\\n\\t".join(out)

TEMPLATE = r'''
typedef unsigned long long u64;
static unsigned tbl[%(TBL)d];
void main(void) {
    unsigned x = 12345;
    for (int i = 0; i < %(TBL)d; i++) {
        x = x * 1664525u + 1013904223u;
        tbl[i] = %(init)s;
    }
    asm volatile ("csrw mhpmevent3, %%0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4, %%0" :: "r"(0x0A));
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    u64 acc = 0;
    asm volatile (
        "mv a0, %%1\n\t"
        "li a1, %(ITER)d\n\t"
        "li a2, 0\n\t"
        "li a3, 0\n\t"
        "li a4, 0\n\t"
        "2:\n\t"
        "%(kern)s\n\t"
        "addi a0, a0, 4\n\t"
        "addi a1, a1, -1\n\t"
        "bnez a1, 2b\n\t"
        "mv %%0, a2\n\t"
        : "=r"(acc) : "r"(tbl) : "a0","a1","a2","a3","a4","t0","t1","memory");
    u64 cycles, instret, br, mp;
    asm volatile ("csrr %%0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %%0, minstret"     : "=r"(instret));
    asm volatile ("csrr %%0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %%0, mhpmcounter4" : "=r"(mp));
    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = 0; *h = 0; *h = 0; *h = 0;
    *h = acc;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = acc;
}
'''
for v in "ABCD":
    init = "1u" if v == "D" else "(x >> 24) & 1u"
    open(f"ldbr_{v}.c", "w").write(TEMPLATE % dict(N=N, ITER=ITER, TBL=TBL, init=init, kern=kernel(v)))
print("generated", N, "sites x", ITER, "iterations")
