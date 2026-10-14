
typedef unsigned long long u64;
static unsigned tbl[1024];
void main(void) {
    unsigned x = 12345;
    for (int i = 0; i < 1024; i++) {
        x = x * 1664525u + 1013904223u;
        tbl[i] = (x >> 24) & 1u;
    }
    asm volatile ("csrw mhpmevent3, %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4, %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    u64 acc = 0;
    asm volatile (
        "mv a0, %1\n\t"
        "li a1, 900\n\t"
        "li a2, 0\n\t"
        "li a3, 0\n\t"
        "li a4, 0\n\t"
        "2:\n\t"
        "lw t0, 0(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 4(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 8(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 12(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 16(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 20(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 24(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 28(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 32(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 36(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 40(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 44(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 48(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 52(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 56(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 60(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 64(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 68(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 72(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 76(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 80(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 84(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 88(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 92(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 96(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 100(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 104(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 108(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 112(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 116(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 120(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 124(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 128(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 132(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 136(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 140(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 144(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 148(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 152(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 156(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 160(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 164(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 168(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 172(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 176(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 180(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 184(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 188(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 192(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 196(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 200(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 204(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 208(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 212(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 216(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 220(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 224(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 228(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 232(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 236(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 240(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 244(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 248(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\tlw t0, 252(a0)\n\tandi t1, t0, 1\n\tbeqz t1, 1f\n\taddi a2, a2, 1\n\t1:\n\t"
        "addi a0, a0, 4\n\t"
        "addi a1, a1, -1\n\t"
        "bnez a1, 2b\n\t"
        "mv %0, a2\n\t"
        : "=r"(acc) : "r"(tbl) : "a0","a1","a2","a3","a4","t0","t1","memory");
    u64 cycles, instret, br, mp;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = 0; *h = 0; *h = 0; *h = 0;
    *h = acc;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = acc;
}
