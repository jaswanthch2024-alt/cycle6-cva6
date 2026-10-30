# D$ line size, AXI width and D$ capacity: Linux boot (measured 2026-10-14)

Design-space exploration on the frozen model (same source as the paper build). CVA6 ships only
16 B lines and a 64-bit AXI bus, so rows B to D have no RTL reference. The I$ stays at 16 B lines
in every row.

## How the builds were made

```bash
# paper build (row A): build_d16_axi64_32k, TIMING_MODEL=CYCLE6_AXI
# rows B-D, in new build directories:
cmake .. -DTIMING_MODEL=CYCLE6_AXI     -DCMAKE_CXX_FLAGS='-DVP_DCACHE_LINE_BYTES=128 -DVP_DCACHE_SETS=32'   # B: 128 B, 64-bit, 32 KB
cmake .. -DTIMING_MODEL=CYCLE6_AXI1024 -DCMAKE_CXX_FLAGS='-DVP_DCACHE_LINE_BYTES=128 -DVP_DCACHE_SETS=32'   # C: 128 B, 1024-bit, 32 KB
cmake .. -DTIMING_MODEL=CYCLE6_AXI1024 -DCMAKE_CXX_FLAGS='-DVP_DCACHE_LINE_BYTES=128 -DVP_DCACHE_SETS=256'  # D: 128 B, 1024-bit, 256 KB
# (all with -DCMAKE_BUILD_TYPE=Release -DUSE_LOCAL_SYSTEMC=ON; D$ is 8-way, so 32 sets x 8 x 128 B = 32 KB)

VP_BOOT_STATS=1 ./RISCV_VP -R 64 --bios boot/fw_jump.bin --dtb boot/vp.dtb \
    --kernel boot/Image --initrd boot/initramfs.cpio.gz
```

## Results (boot to "Starting interactive shell")

| Row | D$ line | AXI | D$ size | Cycles | Instructions | IPC | D$ misses | D$ stall cycles | Cycles per D$ miss | I$ stall cycles |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 16 B | 64-bit | 32 KB | 222,179,066 | 136,596,724 | 0.615 | 1,291,811 | 13,051,915 | 10.1 | 26,942,501 |
| B | 128 B | 64-bit | 32 KB | 225,064,840 | 139,134,104 | 0.618 | 485,393 | 11,824,594 | 24.4 | 28,125,577 |
| C | 128 B | 1024-bit | 32 KB | 214,467,211 | 139,159,092 | 0.649 | 484,918 | 4,428,188 | 9.1 | 23,391,876 |
| D | 128 B | 1024-bit | 256 KB | 208,520,766 | 136,254,506 | 0.653 | 161,304 | 1,497,955 | 9.3 | 23,249,898 |

Notes:

- Cycles per D$ miss = D$ stall cycles / D$ misses.
- The boot executes 1.9% more instructions in rows B and C than in A (timer ticks and scheduling
  follow the cycle count), so compare IPC, not raw cycles.
- Row B shows no gain: D$ misses fall to 0.38x but each costs 2.4x more on a 64-bit bus (16 beats),
  and the longer D$ refills delay I$ refills on the shared port (I$ stall +1.2 M cycles).
- Row C recovers it: a 1024-bit bus makes the 128 B refill one beat; I$ refills also get cheaper.
- The slave latency was fitted for 2-beat bursts; the 16-beat row extrapolates it.

## Cache-only grid at the paper's 64-bit bus (line size x capacity)

Built with `-DTIMING_MODEL=CYCLE6_AXI` and `-DVP_DCACHE_LINE_BYTES=<16|128> -DVP_DCACHE_SETS=<n>`
(8-way D$: 2048 sets of 16 B or 256 sets of 128 B give 256 KB).

| D$ line / size | Cycles | Instructions | IPC | D$ misses | D$ stall cycles | Cycles per D$ miss | I$ stall cycles |
|---|---:|---:|---:|---:|---:|---:|---:|
| 16 B / 32 KB (CVA6, row A) | 222,179,066 | 136,596,724 | 0.615 | 1,291,811 | 13,051,915 | 10.1 | 26,942,501 |
| 128 B / 32 KB (row B) | 225,064,840 | 139,134,104 | 0.618 | 485,393 | 11,824,594 | 24.4 | 28,125,577 |
| 16 B / 256 KB | 219,215,202 | 137,753,239 | 0.628 | 755,992 | 7,645,424 | 10.1 | 26,775,612 |
| 128 B / 256 KB | 215,319,191 | 136,989,231 | 0.636 | 161,272 | 3,994,266 | 24.8 | 26,919,925 |

Bus effect (from the first table): 128 B / 32 KB goes from IPC 0.618 to 0.649 (+5.0%) and from 24.4 to
9.1 cycles per D$ miss with a 1024-bit bus; 128 B / 256 KB goes from 0.636 to 0.653 (+2.7%).
Boot instruction counts differ by up to 1.9% between rows, so compare IPC, not cycles.

## Same four cache configurations on the memory benchmarks (64-bit bus, timed region)

| D$ line / size | locality IPC | locality D$ misses | cache_sweep IPC | cache_sweep D$ misses | boot IPC |
|---|---:|---:|---:|---:|---:|
| 16 B / 32 KB (CVA6) | 0.467 | 262,705 | 0.458 | 433,719 | 0.615 |
| 128 B / 32 KB | 0.635 | 33,205 | 0.620 | 54,426 | 0.618 |
| 16 B / 256 KB | 0.743 | 6,402 | 0.472 | 399,353 | 0.628 |
| 128 B / 256 KB | 0.751 | 802 | 0.628 | 49,965 | 0.636 |

`locality` (96 KB + 4 KB hot) and `cache_sweep` (2 MB + 4 KB hot) are streaming scans: a 128 B line
cuts misses 8x (the line-size ratio), and 256 KB removes the misses of `locality` (it fits) but not
those of `cache_sweep`. The boot has little spatial locality, so the line size alone gains 0.6%.
Instruction counts are identical across rows for both benchmarks. Cycles per D$ miss: 10.0 for 16 B
lines and 24.0 for 128 B lines. Caveat: at the default configuration `locality` is already +7.8%
(IPC) away from the RTL, so its absolute IPC is optimistic.
