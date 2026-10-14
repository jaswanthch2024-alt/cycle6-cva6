// SPDX-License-Identifier: GPL-3.0-or-later
// Interface to the AXI refill model (CYCLE6_AXI builds only). The matchlib
// templates stay in AxiContentionTop.cpp behind a pimpl, so the CPU code
// doesn't have to compile them.
#pragma once
#include <cstdint>

namespace sc_core { class sc_clock; }

namespace riscv_axi {

class AxiContentionTop {
public:
  enum Master { ICACHE = 0, DCACHE = 1, STORE = 2, NUM_MASTERS = 3 };

  // Builds the arbiter, one AXI master per requester and the memory slave,
  // all on clk. slave_latency is the fitted memory latency. Burst lengths are
  // per cache (2 beats for a 16 B line on a 64-bit bus); STORE uses the D$ one.
  AxiContentionTop(sc_core::sc_clock* clk, int slave_latency,
                    int icache_burst_beats = 2, int dcache_burst_beats = 2);
  ~AxiContentionTop();

  // Post a request for master m, then poll done(m). Posting again while it is
  // held does nothing. wdata is the store value for a write; for a read it is
  // the value the CPU already read over TLM, and data() returns it so it can
  // be checked. Pass 0 if it isn't known yet and set it later.
  void request(int m, uint64_t addr, bool is_write = false, uint64_t wdata = 0);
  // Set the value of a request still in flight (DTLB miss followed by a D$
  // miss, where the value is read after the request goes out).
  void set_pending_wdata(int m, uint64_t wdata);
  bool done(int m) const;
  uint64_t data(int m) const;      // valid once done(m)
  void ack(int m);                 // drop the request so the next one can go
  bool busy(int m) const;          // posted and not acked yet

private:
  struct Impl;
  Impl* impl_;
};

} // namespace riscv_axi
