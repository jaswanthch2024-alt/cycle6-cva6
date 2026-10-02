import re
src=open("fpmb_int_d2.c").read()
A=lambda r:'asm volatile ("addi %%0,%%0,1" : "+r"(%s));'%r
def mv(d,s,op="addi %0,%1,1"): return 'asm volatile ("%s" : "=r"(%s) : "r"(%s));'%(op,d,s)
V={}
V["ia_add"]=['asm volatile ("add %0,%0,%1" : "+r"(t) : "r"(t1));']*32
V["ib_xor"]=['asm volatile ("xor %0,%0,%1" : "+r"(t) : "r"(t1));']*32
V["ic_move"]=[mv("t1","t") if i%2==0 else mv("t","t1") for i in range(32)]   # rd!=rs chain
V["id_pair"]=[A(["t","t","t1","t1"][i%4]) for i in range(32)]  # 50% adjacent deps
V["ie_quad"]=[A(["t","t","t","t","t1","t1","t1","t1"][i%8]) for i in range(32)]
V["if_slli"]=['asm volatile ("slli %0,%0,1" : "+r"(t));']*32   # shift chain
V["ig_addw"]=['asm volatile ("addw %0,%0,%1" : "+r"(t) : "r"(t1));']*32
V["ih_mul"]=['asm volatile ("mul %0,%0,%1" : "+r"(t) : "r"(t1));']*32
for n,ops in V.items():
    body="\n        ".join(ops)
    s=re.sub(r'(for \(u64 i = 0; i < ITER; i\+\+\) \{\n).*?(\n    \}\n    asm volatile \("")',lambda m:m.group(1)+"        "+body+m.group(2),src,flags=re.S)
    s=s.replace("u64 t = 0, t1 = 0","u64 t = 1, t1 = 3")
    open("fpmb_"+n+".c","w").write(s)
print(" ".join("fpmb_"+n for n in V))
