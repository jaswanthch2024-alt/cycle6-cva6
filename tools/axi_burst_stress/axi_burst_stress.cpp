// SPDX-License-Identifier: GPL-3.0-or-later
// axi_burst_stress -- a bulk block-transfer stress test to actually
// differentiate AXI bus widths above 128-bit.
//
// Why this tool exists: the shipped CPU benchmarks (mem_contention_bench
// etc.) refill exactly one 16 B D$/I$ line per miss. At 128-bit AXI a 16 B
// line already fits in a single beat -- beats-per-refill floors at 1 and
// cannot go lower, so 128/256/512/1024-bit AXI are provably indistinguishable
// on any single-line-refill workload (see README's width-sweep table). That
// is a structural property of the per-line refill model, not a benchmark
// weakness.
//
// This tool sidesteps the CPU/cache path entirely (same "standalone harness"
// approach as tools/axi_external_demo/, does not touch VPTop/
// CPU_P64_6_Cycle/AxiContentionTop) and drives real multi-beat AXI INCR
// bursts whose total size is swept across 512 B / 4 KB / 64 KB blocks --
// transfer sizes actually larger than one cache line, so a wider bus has
// fewer beats to move the same bytes. AXI4 caps a single INCR burst at 256
// beats (real spec limit, respected here via ar_item.len = beats-1 with beats
// <= 256), so a block larger than 256 beats' worth is split into multiple
// back-to-back bursts, same as real hardware/DMA engines would do.
//
// Built once per AXI width variant (CYCLE6_AXI/AXI128/AXI256/AXI512/AXI1024),
// same as axi_external_demo -- run the resulting binary once per width and
// compare the printed cycles-per-block-size numbers.
#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <systemc.h>

#include "axi/AxiConfigSelect.h"
#include "axi4_segment.h"

#include <cstdint>
#include <cstdio>

// Fixed total data volume moved per block-size phase, so every phase (and
// every width) is compared on equal footing (same bytes, different chunking).
// 256 KiB stays within MinimalAxiMemSlave's smallest backing-store capacity
// (SZ * bytesPerBeat = 65536 * 8 B = 512 KiB at 64-bit AXI, the narrowest
// width this tool is ever built at).
static const uint64_t TOTAL_BYTES = 256 * 1024;
static const uint64_t BLOCK_SIZES[] = {512, 4096, 65536};
static const int MAX_BURST_BEATS = 256; // AXI4 INCR spec limit (8-bit AxLEN encodes len-1)

typedef axi::axi4<riscv_axi::AxiCfg> local_axi;

// A small array-backed AXI4 read subordinate, copied down from
// MinimalAxiMemSlave's read side (no write path needed -- this is a
// read-only bulk-transfer stress test). Kept local to this tool rather than
// reusing MinimalAxiMemSlave directly so the backing store can be sized for
// the largest block swept here without touching the shared header.
class BurstSlave : public sc_module, public axi::axi4_segment<riscv_axi::AxiCfg> {
public:
  sc_in<bool> clk{"clk"};
  sc_in<bool> rst_bar{"rst_bar"};
  r_slave<AUTO_PORT> r_slave0{"r_slave0"};
  static const int SZ = 1 << 16; // beats of backing store
  typedef NVUINTW(axi_cfg::dataWidth) arr_t;
  arr_t *array{nullptr};
  // Per-burst-request response latency in clocks. Was a hardcoded 8 (stale
  // copy of MinimalAxiMemSlave's ORIGINAL default constructor parameter);
  // the real CPU-facing path (CPU_P64_6_Cycle.h's axi_slave_latency) was
  // since re-derived to 0 ("genuine 2-beat AXI burst reads") and this tool
  // was never updated to match, so its cycle numbers were stale relative to
  // every other AXI-timed measurement in this project. Now takes the value
  // as a constructor parameter, default 0 to match the current calibration.
  int latency_;

  SC_HAS_PROCESS(BurstSlave);
  BurstSlave(sc_module_name nm, int latency = 0) : sc_module(nm), latency_(latency) {
    array = new arr_t[SZ];
    for (int i = 0; i < SZ; i++) array[i] = arr_t(i);
    SC_THREAD(slave_r_process);
    sensitive << clk.pos();
    async_reset_signal_is(rst_bar, false);
  }
  ~BurstSlave() override { delete[] array; }

  void slave_r_process() {
    r_slave0.reset();
    wait();
    while (1) {
      ar_payload ar;
      r_slave0.start_multi_read(ar);
      for (int i = 0; i < latency_; i++) wait(); // per-burst-request latency (see latency_ above)
      while (1) {
        r_payload r;
        uint64_t idx = ar.addr / bytesPerBeat;
        r.data = (idx < (uint64_t)SZ) ? array[idx] : arr_t(0);
        if (!r_slave0.next_multi_read(ar, r)) break;
      }
    }
  }
};

class BurstDriver : public sc_module, public axi::axi4_segment<riscv_axi::AxiCfg> {
public:
  sc_in<bool> clk{"clk"};
  sc_in<bool> rst_bar{"rst_bar"};
  r_master<AUTO_PORT> r_master0{"r_master0"};

  r_payload burst_read(uint32_t addr, int beats) {
    ar_payload ar_item;
    ar_item.addr = addr;
    ar_item.len = beats - 1;
    ar_item.burst = Enc::AXBURST::INCR;
    r_master0.ar.Push(ar_item);
    r_payload r;
    for (int beat = 0; beat < beats; beat++) r = r_master0.r.Pop();
    return r;
  }

  void run() {
    r_master0.reset();
    wait(); // let reset settle
    const int bytes_per_beat = axi_cfg::dataWidth / 8;

    std::printf("\n=== axi_burst_stress (AXI data width: %d bits, %d bytes/beat) ===\n",
                (int)axi_cfg::dataWidth, bytes_per_beat);
    std::printf("%-10s %12s %14s %14s\n", "block", "transfers", "cycles", "bytes/cycle");
    std::printf("---------------------------------------------------------\n");

    for (uint64_t block : BLOCK_SIZES) {
      uint64_t num_transfers = TOTAL_BYTES / block;
      sc_time t0 = sc_time_stamp();

      uint32_t addr = 0;
      for (uint64_t t = 0; t < num_transfers; t++) {
        uint64_t remaining = block;
        while (remaining > 0) {
          uint64_t beats_wanted = remaining / bytes_per_beat;
          int beats = (int)(beats_wanted > (uint64_t)MAX_BURST_BEATS ? MAX_BURST_BEATS : beats_wanted);
          if (beats < 1) beats = 1;
          burst_read(addr, beats);
          uint64_t moved = (uint64_t)beats * bytes_per_beat;
          addr += moved;
          remaining -= moved;
        }
      }

      sc_time elapsed = sc_time_stamp() - t0;
      uint64_t cycles = (uint64_t)(elapsed / sc_time(10, SC_NS));
      double bpc = cycles ? (double)TOTAL_BYTES / (double)cycles : 0.0;
      std::printf("%-10llu %12llu %14llu %14.3f\n",
                  (unsigned long long)block, (unsigned long long)num_transfers,
                  (unsigned long long)cycles, bpc);
    }
    sc_stop();
  }

  SC_HAS_PROCESS(BurstDriver);
  BurstDriver(sc_module_name nm) : sc_module(nm) {
    SC_THREAD(run);
    sensitive << clk.pos();
    async_reset_signal_is(rst_bar, false);
  }
};

SC_MODULE(ResetGen) {
  sc_in<bool> clk;
  sc_out<bool> rst_bar;
  void run() {
    rst_bar.write(false);
    for (int i = 0; i < 10; i++) wait();
    rst_bar.write(true);
  }
  SC_CTOR(ResetGen) {
    SC_THREAD(run);
    sensitive << clk.pos();
  }
};

SC_MODULE(BurstTop) {
  sc_in<bool> clk{"clk"};
  sc_signal<bool> rst_bar{"rst_bar"};

  BurstDriver driver;
  BurstSlave slave;
  local_axi::read::chan<> rch{"rch"};
  ResetGen reset_gen;

  SC_CTOR(BurstTop)
      : driver("driver"), slave("external_slave"), reset_gen("reset_gen") {
    driver.clk(clk);
    driver.rst_bar(rst_bar);
    driver.r_master0(rch);

    slave.clk(clk);
    slave.rst_bar(rst_bar);
    slave.r_slave0(rch);

    reset_gen.clk(clk);
    reset_gen.rst_bar(rst_bar);
  }
};

int sc_main(int argc, char *argv[]) {
  sc_clock clk("clk", sc_time(10, SC_NS));
  BurstTop top("top");
  top.clk(clk);
  sc_start();
  return 0;
}
