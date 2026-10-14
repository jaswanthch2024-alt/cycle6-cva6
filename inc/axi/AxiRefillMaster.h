// SPDX-License-Identifier: GPL-3.0-or-later
// One AXI master per requester (I$, D$, store drain), CYCLE6_AXI build only.
// The pipeline talks to it through sc_signals:
//   req_valid, req_addr, req_wdata  in
//   resp_valid, resp_data           out; resp_valid stays up until req_valid drops
// resp_data returns req_wdata, also for reads, so the value the CPU read over
// TLM travels through the channel; the slave's own data is thrown away.
//
// A refill is an INCR burst of LINE_WIDTH/AxiDataWidth beats, as in
// wt_axi_adapter.sv (2 beats for a 16 B line on 64 bits). The burst blocks
// this module's thread while the pipeline polls resp_valid. The extra cycles
// of the signal handshake are absorbed in the fitted slave latency.
#pragma once

#include "axi4_segment.h"
#include "axi/AxiConfigSelect.h"

namespace riscv_axi {

class AxiRefillMaster : public sc_module,
                        public axi::axi4_segment<AxiCfg> {
public:
  sc_in<bool>          clk;
  sc_in<bool>          rst_bar;

  // matchlib AXI manager ports (bound to the arbiter in AxiContentionTop).
  r_master<AUTO_PORT>  r_master0;
  w_master<AUTO_PORT>  w_master0;

  // Handshake with the CPU.
  sc_in<bool>          req_valid;
  sc_in<uint32_t>      req_addr;
  sc_in<bool>          req_is_write; // false=read (I$/D$), true=write (store-drain)
  // Store data, or for a read the value the CPU already read. 64 bits so a
  // ld fits.
  sc_in<uint64_t>      req_wdata;
  sc_out<bool>         resp_valid;
  sc_out<uint64_t>     resp_data;

  // 2 beats per 16 B line on the 64-bit bus, as in CVA6. VP_AXI_BEATS changes
  // it for experiments; other values are not checked against RTL.
  int burst_beats_ = 2;

  SC_HAS_PROCESS(AxiRefillMaster);
  AxiRefillMaster(sc_module_name nm, int burst_beats = 2)
      : sc_module(nm), clk("clk"), rst_bar("rst_bar"),
        r_master0("r_master0"), w_master0("w_master0"),
        req_valid("req_valid"), req_addr("req_addr"),
        req_is_write("req_is_write"), req_wdata("req_wdata"),
        resp_valid("resp_valid"), resp_data("resp_data"),
        burst_beats_(burst_beats) {
    SC_THREAD(master_process);
    sensitive << clk.pos();
    async_reset_signal_is(rst_bar, false);
  }

  // INCR burst: one AR with len = beats-1, then that many R beats. matchlib's
  // r_master only has single_read(), so this uses the ar/r ports directly.
  r_payload burst_read(uint32_t addr) {
    ar_payload ar_item;
    ar_item.addr = addr;
    ar_item.len = burst_beats_ - 1;
    ar_item.burst = Enc::AXBURST::INCR;
    r_master0.ar.Push(ar_item);

    r_payload r;
    for (int beat = 0; beat < burst_beats_; beat++)
      r = r_master0.r.Pop(); // the last beat ends the refill
    return r;
  }

  void master_process() {
    r_master0.reset();
    w_master0.reset();
    resp_valid.write(false);
    resp_data.write(0);
    wait();

    while (1) {
      // Wait for a request from the pipeline.
      while (!req_valid.read()) wait();

      uint32_t addr = req_addr.read();
      if (req_is_write.read()) {
        w_master0.single_write(addr, req_wdata.read()); // blocks through arbiter+slave
        resp_data.write(0);
      } else {
        // The burst gives the timing; its data is dropped and resp_data
        // returns req_wdata instead.
        burst_read(addr);
        resp_data.write(req_wdata.read());
      }

      // Hold resp_valid until the CPU drops req_valid, so the same miss
      // isn't started twice.
      resp_valid.write(true);
      while (req_valid.read()) wait();
      resp_valid.write(false);
    }
  }
};

} // namespace riscv_axi
