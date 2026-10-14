/*
 * dhrystone_bare.c — Dhrystone 2.1 (ANSI), bare-metal for CVA6 co-sim + VP.
 * Self-contained: no libc, no printf. Static allocation (no malloc/alloca).
 * Reports HPM counters via sim_halter (0x90000018), like the other benchmarks.
 *
 * All globals are explicitly zeroed at startup so every address is written
 * before any read (required by the co-sim SlaveFromFile read-address check).
 *
 * HPM mapping (mhpmevent3-8): 0x09 branches, 0x0A mispredicts, 0x01 I$ miss,
 *                             0x02 D$ miss, 0x10 I$ acc, 0x11 D$ acc.
 */

#define NUMBER_OF_RUNS 3000

typedef enum {Ident_1, Ident_2, Ident_3, Ident_4, Ident_5} Enumeration;
typedef int  One_Thirty;
typedef int  One_Fifty;
typedef char Capital_Letter;
typedef int  Boolean;
typedef char Str_30 [31];
typedef int  Arr_1_Dim [50];
typedef int  Arr_2_Dim [50][50];

typedef struct record {
    struct record *Ptr_Comp;
    Enumeration    Discr;
    union {
        struct { Enumeration Enum_Comp; int Int_Comp; char Str_Comp[31]; } var_1;
        struct { Enumeration E_Comp_2; char Str_2_Comp[31]; } var_2;
        struct { char Ch_1_Comp; char Ch_2_Comp; } var_3;
    } variant;
} Rec_Type, *Rec_Pointer;

#define Null 0
#define true 1
#define false 0
#define REG
#define structassign(d, s) (d) = (s)

/* ---- bare-metal libc substitutes ---- */
static char *bm_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
static int   bm_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void  bm_memzero(void *p, unsigned long n) { char *c = (char*)p; while (n--) *c++ = 0; }

/* ---- globals ---- */
Rec_Type        Rec_0, Rec_1;          /* static storage replacing malloc/alloca */
Rec_Pointer     Ptr_Glob, Next_Ptr_Glob;
int             Int_Glob;
Boolean         Bool_Glob;
char            Ch_1_Glob, Ch_2_Glob;
int             Arr_1_Glob [50];
int             Arr_2_Glob [50][50];

Enumeration Func_1 (Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val);
Boolean     Func_2 (Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref);
Boolean     Func_3 (Enumeration Enum_Par_Val);
void Proc_1 (Rec_Pointer Ptr_Val_Par);
void Proc_2 (One_Fifty *Int_Par_Ref);
void Proc_3 (Rec_Pointer *Ptr_Ref_Par);
void Proc_4 (void);
void Proc_5 (void);
void Proc_6 (Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par);
void Proc_7 (One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref);
void Proc_8 (Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref, int Int_1_Par_Val, int Int_2_Par_Val);

void Proc_1 (Rec_Pointer Ptr_Val_Par) {
    REG Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;
    structassign (*Ptr_Val_Par->Ptr_Comp, *Ptr_Glob);
    Ptr_Val_Par->variant.var_1.Int_Comp = 5;
    Next_Record->variant.var_1.Int_Comp = Ptr_Val_Par->variant.var_1.Int_Comp;
    Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
    Proc_3 (&Next_Record->Ptr_Comp);
    if (Next_Record->Discr == Ident_1) {
        Next_Record->variant.var_1.Int_Comp = 6;
        Proc_6 (Ptr_Val_Par->variant.var_1.Enum_Comp, &Next_Record->variant.var_1.Enum_Comp);
        Next_Record->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7 (Next_Record->variant.var_1.Int_Comp, 10, &Next_Record->variant.var_1.Int_Comp);
    } else {
        structassign (*Ptr_Val_Par, *Ptr_Val_Par->Ptr_Comp);
    }
}

void Proc_2 (One_Fifty *Int_Par_Ref) {
    One_Fifty Int_Loc;
    Enumeration Enum_Loc = Ident_1;
    Int_Loc = *Int_Par_Ref + 10;
    do {
        if (Ch_1_Glob == 'A') {
            Int_Loc -= 1;
            *Int_Par_Ref = Int_Loc - Int_Glob;
            Enum_Loc = Ident_1;
        }
    } while (Enum_Loc != Ident_1);
}

void Proc_3 (Rec_Pointer *Ptr_Ref_Par) {
    if (Ptr_Glob != Null)
        *Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
    Proc_7 (10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

void Proc_4 (void) {
    Boolean Bool_Loc;
    Bool_Loc = Ch_1_Glob == 'A';
    Bool_Glob = Bool_Loc | Bool_Glob;
    Ch_2_Glob = 'B';
}

void Proc_5 (void) {
    Ch_1_Glob = 'A';
    Bool_Glob = false;
}

void Proc_6 (Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par) {
    *Enum_Ref_Par = Enum_Val_Par;
    if (! Func_3 (Enum_Val_Par))
        *Enum_Ref_Par = Ident_4;
    switch (Enum_Val_Par) {
        case Ident_1: *Enum_Ref_Par = Ident_1; break;
        case Ident_2: *Enum_Ref_Par = (Int_Glob > 100) ? Ident_1 : Ident_4; break;
        case Ident_3: *Enum_Ref_Par = Ident_2; break;
        case Ident_4: break;
        case Ident_5: *Enum_Ref_Par = Ident_3; break;
    }
}

void Proc_7 (One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref) {
    One_Fifty Int_Loc;
    Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

void Proc_8 (Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref, int Int_1_Par_Val, int Int_2_Par_Val) {
    REG One_Fifty Int_Index;
    REG One_Fifty Int_Loc;
    Int_Loc = Int_1_Par_Val + 5;
    Arr_1_Par_Ref [Int_Loc] = Int_2_Par_Val;
    Arr_1_Par_Ref [Int_Loc+1] = Arr_1_Par_Ref [Int_Loc];
    Arr_1_Par_Ref [Int_Loc+30] = Int_Loc;
    for (Int_Index = Int_Loc; Int_Index <= Int_Loc+1; ++Int_Index)
        Arr_2_Par_Ref [Int_Loc] [Int_Index] = Int_Loc;
    Arr_2_Par_Ref [Int_Loc] [Int_Loc-1] += 1;
    Arr_2_Par_Ref [Int_Loc+20] [Int_Loc] = Arr_1_Par_Ref [Int_Loc];
    Int_Glob = 5;
}

Enumeration Func_1 (Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val) {
    Capital_Letter Ch_1_Loc, Ch_2_Loc;
    Ch_1_Loc = Ch_1_Par_Val;
    Ch_2_Loc = Ch_1_Loc;
    if (Ch_2_Loc != Ch_2_Par_Val)
        return (Ident_1);
    else {
        Ch_1_Glob = Ch_1_Loc;
        return (Ident_2);
    }
}

Boolean Func_2 (Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref) {
    REG One_Thirty Int_Loc;
    Capital_Letter Ch_Loc = ' ';
    Int_Loc = 2;
    while (Int_Loc <= 2)
        if (Func_1 (Str_1_Par_Ref[Int_Loc], Str_2_Par_Ref[Int_Loc+1]) == Ident_1) {
            Ch_Loc = 'A';
            Int_Loc += 1;
        }
    if (Ch_Loc >= 'W' && Ch_Loc < 'Z')
        Int_Loc = 7;
    if (Ch_Loc == 'R')
        return (true);
    else {
        if (bm_strcmp (Str_1_Par_Ref, Str_2_Par_Ref) > 0) {
            Int_Loc += 7;
            Int_Glob = Int_Loc;
            return (true);
        } else
            return (false);
    }
}

Boolean Func_3 (Enumeration Enum_Par_Val) {
    Enumeration Enum_Loc;
    Enum_Loc = Enum_Par_Val;
    if (Enum_Loc == Ident_3)
        return (true);
    return (false);
}

void main (void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");

    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01));
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02));
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10));
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11));

    One_Fifty   Int_1_Loc, Int_2_Loc, Int_3_Loc;
    char        Ch_Index;
    Enumeration Enum_Loc;
    Str_30      Str_1_Loc, Str_2_Loc;
    int         Run_Index;

    /* Zero ALL globals so every address is written before any read. */
    bm_memzero(&Rec_0, sizeof(Rec_Type));
    bm_memzero(&Rec_1, sizeof(Rec_Type));
    bm_memzero(Arr_1_Glob, sizeof(Arr_1_Glob));
    bm_memzero(Arr_2_Glob, sizeof(Arr_2_Glob));
    Int_Glob = 0; Bool_Glob = 0; Ch_1_Glob = 0; Ch_2_Glob = 0;

    Next_Ptr_Glob = &Rec_1;
    Ptr_Glob      = &Rec_0;
    Ptr_Glob->Ptr_Comp                = Next_Ptr_Glob;
    Ptr_Glob->Discr                   = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp  = 40;
    bm_strcpy (Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    bm_strcpy (Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");
    Arr_2_Glob [8][7] = 10;

    /* Zero counters just before the measured loop. */
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero");
    asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero");
    asm volatile ("csrw mhpmcounter8, zero");
    unsigned long long c0, i0;
    asm volatile ("csrr %0, mcycle"   : "=r"(c0));
    asm volatile ("csrr %0, minstret" : "=r"(i0));

    for (Run_Index = 1; Run_Index <= NUMBER_OF_RUNS; ++Run_Index) {
        Proc_5();
        Proc_4();
        Int_1_Loc = 2;
        Int_2_Loc = 3;
        bm_strcpy (Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        Enum_Loc = Ident_2;
        Bool_Glob = ! Func_2 (Str_1_Loc, Str_2_Loc);
        while (Int_1_Loc < Int_2_Loc) {
            Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
            Proc_7 (Int_1_Loc, Int_2_Loc, &Int_3_Loc);
            Int_1_Loc += 1;
        }
        Proc_8 (Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc);
        Proc_1 (Ptr_Glob);
        for (Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index) {
            if (Enum_Loc == Func_1 (Ch_Index, 'C')) {
                Proc_6 (Ident_1, &Enum_Loc);
                bm_strcpy (Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                Int_2_Loc = Run_Index;
                Int_Glob = Run_Index;
            }
        }
        Int_2_Loc = Int_2_Loc * Int_1_Loc;
        Int_1_Loc = Int_2_Loc / Int_3_Loc;
        Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
        Proc_2 (&Int_1_Loc);
    }

    unsigned long long cycles, instret, branches, mispredicts, imiss, dmiss, iacc, dacc;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(branches));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mispredicts));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(imiss));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dmiss));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(iacc));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(dacc));

    /* prevent the optimiser from discarding the benchmark result */
    asm volatile ("" :: "r"(Int_Glob), "r"(Bool_Glob), "r"(c0), "r"(i0));

    volatile unsigned long long *halter = (volatile unsigned long long *)0x90000018ULL;
    *halter = cycles;
    *halter = instret;
    *halter = branches;
    *halter = mispredicts;
    *halter = imiss;
    *halter = dmiss;
    *halter = iacc;
    *halter = dacc;
    *halter = 0xE000000CULL;
    /* VP clean halt via HTIF tohost (0x80001000); harmless dram write on co-sim. */
    *(volatile unsigned long long *)0x80001000ULL = 1;
    while (1) {}
}
