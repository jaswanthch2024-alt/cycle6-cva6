import re
src=open("fpmb_int_d2.c").read()
def asm(s): return 'asm volatile ("%s" ::: "a0","a2","a3","a4","a5","a6","a7");'%s
V={
 "pa_rdrs2":  ["xor a0,a2,a3","add a5,a0,a5"],     # int_alu pattern: consumer rd==its rs2
 "pb_fresh":  ["xor a0,a2,a3","add a4,a0,a5"],     # consumer rd fresh
 "pc_rdrs1":  ["add a0,a2,a3","add a0,a0,a5"],     # consumer rd==rs1 (dep operand)
 "pd_addfresh":["add a0,a2,a3","add a4,a0,a5"],
 "pe_dep_rs2":["add a0,a2,a3","add a4,a5,a0"],     # dependence via rs2
 "pf_indep":  ["add a0,a2,a3","add a4,a6,a5"],     # no dependence (control)
}
for n,pair in V.items():
    body="\n        ".join(asm(x) for _ in range(16) for x in pair)
    s=re.sub(r'(for \(u64 i = 0; i < ITER; i\+\+\) \{\n).*?(\n    \}\n    asm volatile \("")',lambda m:m.group(1)+"        "+body+m.group(2),src,flags=re.S)
    open("fpmb_"+n+".c","w").write(s)
print(" ".join("fpmb_"+n for n in V))
