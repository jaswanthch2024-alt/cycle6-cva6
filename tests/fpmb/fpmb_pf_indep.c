
typedef unsigned long long u64;
#define ITER 10000
void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");
    asm volatile ("li t0, 0x6000\n\tcsrs mstatus, t0" ::: "t0");
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmcounter3, zero"); asm volatile ("csrw mhpmcounter4, zero");
    u64 t = 0, t1 = 0, t2 = 0; double a = 1.5, b = 1.0000001, c0=1.1,c1=1.2,c2=1.3,c3=1.4,c4=1.5,c5=1.6,c6=1.7,c7=1.8;
    for (u64 i = 0; i < ITER; i++) {
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a0,a2,a3" ::: "a0","a2","a3","a4","a5","a6","a7");
        asm volatile ("add a4,a6,a5" ::: "a0","a2","a3","a4","a5","a6","a7");
    }
    asm volatile ("" : : "r"(t), "r"(t1), "r"(t2), "f"(a+c0+c1+c2+c3+c4+c5+c6+c7));
    u64 cycles, instret, br, mp;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    double acc = a+c0+c1+c2+c3+c4+c5+c6+c7;
    u64 result; asm volatile ("fmv.x.d %0, %1" : "=r"(result) : "f"(acc));
    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = 0; *h = 0; *h = 0; *h = 0;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;
}
