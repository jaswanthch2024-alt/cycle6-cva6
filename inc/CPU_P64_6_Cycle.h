// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file CPU_P64_6_Cycle.h
 * @brief Cycle6: six-stage RV64 pipeline model of CVA6
 *
 * Stages: PCGen -> Fetch -> (4-entry fetch queue) -> ID -> Issue -> EX -> Commit
 */
#pragma once
#ifndef CPU_P64_6_CYCLE_H
#define CPU_P64_6_CYCLE_H

#define SC_INCLUDE_DYNAMIC_PROCESSES
#include "systemc"
#include "tlm.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <fstream>
#include <cstdio>
#include "tlm_utils/simple_initiator_socket.h"

#include "CPU.h"
#include "Instruction.h"
#include "MemoryInterface.h"
#include "Registers.h"
#include "Memory.h"
#include "BASE_ISA.h"
#include "C_extension.h"
#include "M_extension.h"
#include "A_extension.h"
#include "F_extension.h"
#include "D_extension.h"
#include "Performance.h"
#include "Scoreboard.h"
#include "StoreBuffer.h"
#include "Cache.h"
#include "CSR_File.h"
#include "MMU.h"
#ifdef ENABLE_AXI_CONTENTION
#include "axi/AxiContentionTop.h"
#include "axi/AxiConfigSelect.h"
#endif

namespace riscv_tlm {

namespace peripherals { class CLINT; }  // forward declaration

class CPURV64P6_Cycle : public CPU {
public:
    using BaseType = std::uint64_t;

    SC_HAS_PROCESS(CPURV64P6_Cycle);

    CPURV64P6_Cycle(sc_core::sc_module_name const& name, BaseType PC, bool debug);
    ~CPURV64P6_Cycle() override;

    void set_clock(sc_core::sc_clock* c) override;
    bool CPU_step() override { return false; }
    bool cpu_process_IRQ() override;
    void call_interrupt(tlm::tlm_generic_payload& m_trans, sc_core::sc_time& delay) override;

    std::uint64_t getStartDumpAddress() override;
    std::uint64_t getEndDumpAddress() override;
    bool isPipelined() const override { return true; }

    struct Stats {
        uint64_t cycles{0};
        uint64_t instructions{0};
        uint64_t stalls{0};
        uint64_t branches{0};
        uint64_t branch_mispredicts{0};
        // Diagnostic split of branch_mispredicts by kind (EX + ID sites)
        uint64_t mp_cond{0}, mp_jal{0}, mp_ret{0}, mp_jalr{0}, mp_alias{0};
        uint64_t flushes{0};
        uint64_t forwarded_reads{0};     // operands taken from the forwarding path
        uint64_t load_store_forwards{0}; // loads served from the store buffer
        uint64_t mul_stall_cycles{0};    // cycles the multiplier was busy
        uint64_t div_stall_cycles{0};    // cycles the divider was busy
        uint64_t icache_misses{0};
        uint64_t icache_miss_cycles{0};
        uint64_t dcache_misses{0};
        uint64_t dcache_miss_cycles{0};
        uint64_t load_use_stall_cycles{0}; // cycles with a D$-hit load still in flight
        uint64_t itlb_miss_cycles{0};    // nominal PTW cycles, not necessarily stalled (AXI build)
        uint64_t dtlb_miss_cycles{0};
        uint64_t m_instrs{0};            // committed per privilege mode
        uint64_t s_instrs{0};
        uint64_t u_instrs{0};
        uint64_t dual_commits{0};        // cycles that committed two instructions
        uint64_t load_addr_events{0};    // load->address bubbles charged (BUGS.md B9)

        double get_cpi() const { return instructions > 0 ? (double)cycles / instructions : 0; }
        double get_ipc() const { return cycles > 0 ? (double)instructions / cycles : 0; }
    };
    Stats stats;
    const Stats& getStats() const { return stats; }

    void printStats() const;
    void dumpPipelineTrace(const std::string& filename) const;

    size_t trace_limit{100000};  // Max cycles to record: settable before sc_start()
    size_t trace_idx{0};         // Circular trace buffer index
    peripherals::CLINT* clint_ptr{nullptr};  // for WFI fast-forward
    Registers<BaseType>*    register_bank{nullptr};

    // --- Debug: recent store log (circular buffer for SLUB crash diagnosis) ---
    struct RecentStore {
        uint64_t pa;
        uint64_t data;
        int      size;
        uint64_t pc;
        uint64_t cycle;
    };
    static constexpr size_t RECENT_STORE_LOG_SIZE = 64;
    RecentStore recent_stores[RECENT_STORE_LOG_SIZE]{};
    size_t recent_store_idx{0};

private:
    // =========================================================================
    // Components
    // =========================================================================
    
    BASE_ISA<BaseType>*     base_inst{nullptr};
    A_extension<BaseType>*  a_inst{nullptr};
    C_extension<BaseType>*  c_inst{nullptr};
    M_extension<BaseType>*  m_inst{nullptr};
    F_extension<BaseType>*  f_inst{nullptr};
    D_extension<BaseType>*  d_inst{nullptr};

    BaseType int_cause{0};
    
    sc_core::sc_clock* clk{nullptr};
    sc_core::sc_time clock_period{10, sc_core::SC_NS};

    // =========================================================================
    // Pipeline latches. Each stage writes *_next; the clock edge in
    // cycle_thread copies it to the *_reg the next stage reads.
    // =========================================================================

    // PCGen -> Fetch Latch
    // Holds the next program counter to be fetched.
    struct PCGen_Fetch_Latch {
        uint64_t pc{0};    // Address to fetch from
        bool valid{false}; // Start fetching?
        
        // Branch Prediction Metadata
        bool predicted_taken{false};
        uint64_t predicted_target{0};
        bool is_ras_call{false};
        bool is_ras_return{false};
    } pcgen_fetch_reg, pcgen_fetch_next;


    // Fetch -> ID Latch
    // Holds the fetched instruction and its PC.
    struct Fetch_ID_Latch {
        uint64_t pc{0};
        uint32_t instr{0}; // Raw instruction data (lower 16 bits for compressed)
        bool valid{false};
        bool is_compressed{false}; // True if this is a 16-bit C-extension instruction
        
        // Branch Prediction Metadata
        bool predicted_taken{false};
        uint64_t predicted_target{0};
        bool is_ras_call{false};
        bool is_ras_return{false};
    } fetch_id_reg, fetch_id_next;

    // ID -> Issue Latch
    // Holds decoded instruction details ready for dispatch.
    struct ID_Issue_Latch {
        uint64_t pc{0};
        uint32_t instr{0};
        uint8_t rd{0}, rs1{0}, rs2{0}; // Register indices
        int64_t imm{0};                // Decoded immediate
        uint8_t opcode{0};
        uint8_t funct3{0};
        uint8_t funct7{0};
        bool valid{0};
        bool is_compressed{false};
        bool is_illegal{false};  // True for unrecognized/unsupported instructions

        // Branch Prediction Metadata
        bool predicted_taken{false};
        uint64_t predicted_target{0};
        bool is_ras_call{false};
        bool is_ras_return{false};
    } id_issue_reg, id_issue_next;

    // Issue -> EX Latch
    // Holds dispatched instruction with operands read from register bank.
    // Also contains the allocated ROB index for retirement.
    struct Issue_EX_Latch {
        uint64_t pc{0};
        uint32_t instr{0};         // Raw encoding (for tval in illegal-instruction traps)
        uint64_t rs1_val{0};
        uint64_t rs2_val{0};
        int64_t imm{0};
        uint8_t rd{0};
        uint8_t rs1{0};            // Register index (needed by CSR-imm and SFENCE.VMA)
        uint8_t rs2{0};            // Register index
        uint8_t opcode{0};
        uint8_t funct3{0};
        uint8_t funct7{0};
        int rob_index{-1};   // Index in Reorder Buffer (for tracking completion)
        bool valid{false};
        bool is_compressed{false}; // Needed for correct JAL/JALR link address (+2 vs +4)
        
        // Branch Prediction Metadata
        bool predicted_taken{false};
        uint64_t predicted_target{0};
        bool is_ras_call{false};
        bool is_ras_return{false};
    } issue_ex_reg, issue_ex_next;

    // There is no EX -> Commit latch: EX writes results into the scoreboard
    // and Commit retires from there.

    // =========================================================================
    // Control & State
    // =========================================================================
    uint64_t next_pc{0};           // Next Program Counter (calculated by PCGen)
    
    // Stall Signals affecting various stages
    bool stall_pcgen{false};
    bool stall_fetch{false};
    bool stall_issue{false};
    bool stall_ex{false};     // EX replay: partial store-buffer overlap -> retry next cycle
    
    // Flush Signal (e.g. for Branch Misprediction)
    bool flush_pipeline{false};
    uint64_t pc_redirect_target{0};
    bool pc_redirect_valid{false};

    // Taken-branch redirect bubble: a correctly-predicted TAKEN branch/jump
    // costs one fetch cycle to steer the frontend to the target (matches CVA6's
    // BTB redirect latency). Set when PCGen predicts taken; consumed as a bubble
    // on the next PCGen cycle.
    bool taken_redirect_bubble{false};
    // Set together with flush_pipeline only for branch/jump mispredicts, so
    // the redirect bubble isn't charged on traps (BUGS.md B6).
    bool flush_is_branch_mispredict{false};
  int mul_waw_rd{-1}; // rd of a multiply that completed this cycle (WAW bubble, BUGS.md B12)
 
    // Scoreboard: unified issue FIFO + reorder buffer + rd_clobber (CVA6-aligned).
    // Replaces the separate bool scoreboard[32] and ReorderBuffer<32> rob.
    Scoreboard<32> scoreboard;

    // Commit-stage capture (reset each cycle in cycle_thread)
    uint32_t commit_count_this_cycle{0};
    uint64_t committed_pc_this_cycle{0};

    // Legacy VP predictor, only used with VP_BP=legacy: tagged 32-entry BTB
    // and 128-entry BHT indexed by PC[6:2] / PC[8:2], 2-entry RAS.
    struct BTBEntry {
        uint64_t target{0};
        uint64_t tag{0};       // full PC
        bool valid{false};
        bool is_return{false};
    };
    BTBEntry btb[32];
    uint8_t bht[128];          // 2-bit counters, 0..1 not taken, 2..3 taken
    std::vector<uint64_t> ras;

    // Default predictor: CVA6's frontend rules (cva6/core/frontend/).
    // instr_scan decodes the fetched instruction; jal is always taken;
    // branches use the BHT (valid bit + 2-bit counter, reset 00, PC[7:1]) or,
    // when the entry is invalid, backward-taken; returns use a 2-entry RAS that
    // is never flushed; other jalr use a tagless 32-entry BTB (PC[5:1]).
    // bht.sv has a 2'b10 reset value in its flush branch, but the frontend ties
    // flush_bp_i to 0, so entries really start at 00.
    struct Cva6Bht { bool valid{false}; uint8_t ctr{0}; };
    struct Cva6Btb { bool valid{false}; uint64_t target{0}; };
    struct Cva6Ras { bool valid{false}; uint64_t ra{0}; };
    Cva6Bht c6_bht[128];
    Cva6Btb c6_btb[32];
    Cva6Ras c6_ras[2];
    bool bp_cva6{true};    // CVA6 frontend rules (VP_BP=legacy for the old predictor)
    bool cva6_predict(uint64_t pc, uint32_t instr, bool is_c, uint64_t &target);
    bool fetch_fault_nonspeculative() const;
    static bool cva6_is_return(uint32_t instr, bool is_c);

    // =========================================================================
    // Pipeline Trace (for visualization)
    // =========================================================================
    struct PipelineTraceEntry {
        uint64_t cycle;
        uint64_t pc_pcgen;     // PC in PCGen stage (0 if invalid)
        uint64_t pc_fetch;     // PC in Fetch stage
        uint64_t pc_id;        // PC in Decode stage
        uint64_t pc_issue;     // PC in Issue stage
        uint64_t pc_ex;        // PC in Execute stage
        uint64_t pc_commit;    // PC being committed this cycle (0 if none)
        bool pcgen_valid;
        bool fetch_valid;
        bool id_valid;
        bool issue_valid;
        bool ex_valid;
        uint32_t commit_valid;     // Number of instructions committed this cycle
        bool is_stall_pcgen;
        bool is_stall_fetch;
        bool is_stall_issue;
        bool is_flush;
    };
    std::vector<PipelineTraceEntry> pipeline_trace;

    // =========================================================================
    // Execution units and buffers
    // =========================================================================

    StoreBuffer<4, 4> store_buffer;   // 4 speculative + 4 committed, as in CVA6

    // A multi-cycle unit holding one in-flight result. Dependent instructions
    // wait through the scoreboard; independent ones keep issuing.
    struct FunctionalUnitState {
        bool     busy{false};
        int      remaining{0};   // cycles until scoreboard.complete()
        uint64_t result{0};
        int      trans_id{-1};
        uint8_t  rd{0};
    };
    FunctionalUnitState mul_fu;        // 2-cycle multiplier
    FunctionalUnitState div_fu;        // serial divider, operand-dependent latency
    // FPnew add/mul/fma/convert ops: one issue per cycle, latency 1-4 cycles
    // (tables in F_extension.h / D_extension.h). One slot per op in flight.
    static constexpr int FPU_SLOTS = 8;
    FunctionalUnitState fpu_pipe[FPU_SLOTS];
    bool fpu_pipe_full() const {
        for (int i = 0; i < FPU_SLOTS; i++) if (!fpu_pipe[i].busy) return false;
        return true;
    }
    bool fpu_any_busy() const {
        for (int i = 0; i < FPU_SLOTS; i++) if (fpu_pipe[i].busy) return true;
        return fpu_divsqrt_fu.busy || fpu_divsqrt_hold.busy;
    }
    // FP div/sqrt is not pipelined. While it runs, add/mul keep issuing; a
    // second div/sqrt waits in the one-entry hold register (fpu_wrap.sv).
    FunctionalUnitState fpu_divsqrt_fu;
    FunctionalUnitState fpu_divsqrt_hold;
    // VP_FPU_LEGACY_BLOCK=1: old model, every FPU op waits for div/sqrt.
    static bool fpu_legacy_block() { static const bool v = [] { const char *e = std::getenv("VP_FPU_LEGACY_BLOCK"); return e && *e == '1'; }(); return v; }
    // Outstanding D$ misses. The WT D$ has a single MSHR (mshr_q), so the AXI
    // build defaults to one slot. VP_DCACHE_SLOTS allows more as an experiment
    // only: all slots share one AXI request channel, so this is not a model of
    // a multi-entry MSHR and it is not validated against RTL.
    static constexpr int MAX_DCACHE_MISS_SLOTS = 8;
#ifdef ENABLE_AXI_CONTENTION
    static constexpr int DCACHE_MISS_SLOTS_DEFAULT = 1;
#else
    static constexpr int DCACHE_MISS_SLOTS_DEFAULT = 2;
#endif
    int dcache_miss_slots_active{DCACHE_MISS_SLOTS_DEFAULT};
    FunctionalUnitState dcache_miss_fu[MAX_DCACHE_MISS_SLOTS];

    // VP_DCACHE_TRACE=<file> / VP_ICACHE_TRACE=<file> write every D$ address
    // or fetch PC, one per line, for tools/cache_sweep/. Output only.
    std::FILE* dcache_trace_file{nullptr};
    void trace_dcache_access(uint64_t addr) {
        if (dcache_trace_file) std::fprintf(dcache_trace_file, "%lx\n", (unsigned long)addr);
    }

    std::FILE* icache_trace_file{nullptr};
    void trace_icache_access(uint64_t addr) {
        if (icache_trace_file) std::fprintf(icache_trace_file, "%lx\n", (unsigned long)addr);
    }
    bool dcache_miss_fu_full() const {
        for (int i = 0; i < dcache_miss_slots_active; i++) if (!dcache_miss_fu[i].busy) return false;
        return true;
    }
    bool dcache_miss_fu_any_busy() const {
        for (int i = 0; i < dcache_miss_slots_active; i++) if (dcache_miss_fu[i].busy) return true;
        return false;
    }
    int dcache_miss_free_slot() const {
        for (int i = 0; i < dcache_miss_slots_active; i++) if (!dcache_miss_fu[i].busy) return i;
        return -1;
    }
    // Loads that hit the D$ or forward from the store buffer. CVA6's hit
    // latency is 3 cycles but pipelined, so this is a small pipeline with one
    // slot per cycle of extra latency (load_hit_penalty, VP_LOAD_LAT; BUGS.md B11).
    static constexpr int LOAD_HIT_SLOTS = 4;
    FunctionalUnitState load_hit_pipe[LOAD_HIT_SLOTS];
    int load_hit_slots_active{2};
    int dmiss_extra{5};   // D$ miss fill tail after the AXI refill; result usable after it (VP_DMISS_EXTRA)
    int dmiss_block{3};   // of those, cycles the miss path stays occupied (VP_DMISS_BLOCK)
    static constexpr int DMISS_HOLD = -2; // trans_id marker: slot only holding for the fill
    // Results of D$ misses whose refill has returned but whose fill tail
    // (dmiss_extra) is still running; the miss slot itself is already free.
    static constexpr int DMISS_TAIL_SLOTS = 4;
    FunctionalUnitState dmiss_tail[DMISS_TAIL_SLOTS];
    bool dmiss_tail_any_busy() const {
        for (const auto &d : dmiss_tail) if (d.busy) return true;
        return false;
    }
    bool load_hit_full() const {
        for (int i = 0; i < load_hit_slots_active; i++) if (!load_hit_pipe[i].busy) return false;
        return true;
    }
    bool load_hit_any_busy() const {
        for (int i = 0; i < LOAD_HIT_SLOTS; i++) if (load_hit_pipe[i].busy) return true;
        return false;
    }
    int load_hit_free_slot() const {
        for (int i = 0; i < load_hit_slots_active; i++) if (!load_hit_pipe[i].busy) return i;
        return -1;
    }

    struct FetchQueueEntry {
        uint64_t pc{0};
        uint32_t instr{0};
        bool is_compressed{false};
        bool predicted_taken{false};
        uint64_t predicted_target{0};
        bool is_ras_call{false};
        bool is_ras_return{false};
    };
    std::vector<FetchQueueEntry> fetch_queue;
    static constexpr size_t FETCH_QUEUE_CAPACITY{4};

    // L1 geometry of cv64a6_imafdc_sv39 (128-bit = 16 B lines, 256 sets):
    // I$ 16 KB 4-way, D$ 32 KB 8-way. D$ sets and line size can be overridden
    // at build time, e.g. -DVP_DCACHE_LINE_BYTES=128 -DVP_DCACHE_SETS=32
    // keeps 32 KB.
#ifndef VP_DCACHE_SETS
#define VP_DCACHE_SETS 256
#endif
#ifndef VP_DCACHE_LINE_BYTES
#define VP_DCACHE_LINE_BYTES 16
#endif
    Cache<256, 4, 16> icache;
    Cache<VP_DCACHE_SETS, 8, VP_DCACHE_LINE_BYTES> dcache;

#ifdef ENABLE_AXI_CONTENTION
    // matchlib AXI arbiter shared by I$ and D$ refills (CYCLE6_AXI builds).
    // Built in set_clock() because it needs the clock. Timing only; the data
    // still goes over TLM.
    riscv_axi::AxiContentionTop* axi_top{nullptr};
    bool icache_axi_pending{false}; // I$ refill in flight
    int  axi_slave_latency{0};      // fitted to the RTL co-sim for 2-beat bursts
#endif

    int icache_miss_remaining{0};
    // Fixed miss penalties, used only by the non-AXI build. Fitted to the
    // co-sim's zero-wait-state memory, not to real DRAM.
    int icache_miss_penalty{7};
    int dcache_miss_penalty{10};
    int store_drain_remaining{0};
    int store_write_penalty{0};    // stores drain to the WT D$ with no stall
    int load_hit_penalty{2};       // extra cycles on a D$ hit: RTL hit latency is 3, pipelined (BUGS.md B11)
    // A load result that feeds a later load/store address costs about 2 more
    // cycles in CVA6 than one feeding an ALU op (measured on a pointer chase,
    // BUGS.md B9). 0 turns it off.
    int load_addr_penalty{2};
    uint32_t reg_load_tainted{0};  // bit per register: value came from a recent load
    int      load_addr_extra{0};   // remaining load->address stall cycles
    // Diagnostic counters, printed with the statistics; no effect on timing.
    uint64_t diag_costall_cycles{0};     // I$ and D$ miss both active
    uint64_t diag_zero_commit_cycles{0};
    uint64_t diag_if_empty_cycles{0};    // fetch queue empty when ID wants an instruction
    uint64_t diag_stall_run_len{0};      // zero-commit runs: current length,
    uint64_t diag_stall_run_count{0};    // count,
    uint64_t diag_stall_run_sum{0};      // total length
    uint64_t diag_stall_run_max{0};      // and longest

    CSR_File csr;                  // M, S and U mode CSRs

    MMU* mmu{nullptr};             // sv39: ITLB, DTLB, page-table walker; created in the constructor

    // Pending trap from EX stage: if valid, PCGen redirects and flushes.
    bool     trap_pending{false};
    uint64_t trap_vector_addr{0};

    struct PendingCSRWrite {
        bool     valid{false};
        uint16_t addr{0};
    };
    PendingCSRWrite pending_csr_write;
    bool wfi_stall{false};
    bool     pending_priv_switch{false};
    uint64_t pending_priv_switch_epc{0};
    uint64_t load_reservation_addr{0};
    bool     load_reservation_valid{false};
    uint64_t pc_current{0};

    // =========================================================================
    // Stage Methods
    // =========================================================================
    void PCGen_stage();
    void Fetch_stage();
    void ID_stage();
    void Issue_stage();
    void EX_stage();     // Includes Memory Access (LSU)
    void Commit_stage(); // Handles Retirement

    void cycle_thread();

    // =========================================================================
    // Helpers
    // =========================================================================
    bool fetch_instruction(uint64_t addr, uint32_t& data);
    // DMI uses base class members: dmi_ptr_valid, dmi_ptr (inherited from CPU)
    uint64_t dmi_start_addr{0};
    uint64_t dmi_end_addr{0};
};

} // namespace riscv_tlm

#endif // CPU_P64_6_CYCLE_H