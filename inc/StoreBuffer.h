// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef STOREBUFFER_H
#define STOREBUFFER_H

#include <cstdint>
#include <array>
#include "Scoreboard.h"

namespace riscv_tlm {

struct StoreBufferEntry {
    bool     valid{false};
    uint64_t address{0};
    uint64_t data{0};
    int      size{0};       // 1, 2, 4 or 8 bytes
    int      trans_id{-1};  // scoreboard id
};

// Store buffer split like CVA6's: stores enter the speculative FIFO in EX and
// move to the committed FIFO at commit, which drains one store per cycle.
// Both are ring buffers so age order survives slot reuse (BUG-11).
// Speculative stores are younger than committed ones, so forwarding searches
// the speculative FIFO first.
template<std::size_t SPEC_DEPTH = 4, std::size_t COMMIT_DEPTH = 4>
class StoreBuffer {
public:
    StoreBuffer() = default;

    // Called in EX once the address is known. Returns the slot, or -1 if full.
    int add_store(uint64_t address, uint64_t data, int size, int trans_id) {
        if (spec_count == SPEC_DEPTH) return -1;
        spec[spec_tail] = {true, address, data, size, trans_id};
        int slot = static_cast<int>(spec_tail);
        spec_tail = (spec_tail + 1) % SPEC_DEPTH;
        spec_count++;
        return slot;
    }

    // Called at commit. Commit is in order, so the store being committed is
    // always at spec_head. Returns false if the committed FIFO is full and
    // commit has to stall.
    bool commit_store(int /*trans_id*/) {
        if (spec_count == 0)            return false;
        if (commit_count == COMMIT_DEPTH) return false;
        committed[commit_tail] = spec[spec_head];
        committed[commit_tail].valid = true;
        commit_tail = (commit_tail + 1) % COMMIT_DEPTH;
        commit_count++;
        spec[spec_head].valid = false;
        spec_head = (spec_head + 1) % SPEC_DEPTH;
        spec_count--;
        return true;
    }

    // Write out the oldest committed store; called once per cycle. Entries
    // invalidated by an AMO are skipped.
    bool drain_one(uint64_t& addr, uint64_t& data, int& size) {
        while (commit_count > 0) {
            addr = committed[commit_head].address;
            data = committed[commit_head].data;
            size = committed[commit_head].size;
            bool was_valid = committed[commit_head].valid;

            committed[commit_head].valid = false;
            commit_head = (commit_head + 1) % COMMIT_DEPTH;
            commit_count--;

            if (was_valid) {
                return true;
            }
        }
        return false;
    }

    enum class FwdResult {
        HIT,             // load fully inside one store, fwd_data valid
        PARTIAL_OVERLAP, // overlaps but not contained, load has to stall
        MISS             // no overlap, read memory
    };

    // Youngest store first. A load that only partly overlaps a store can't be
    // forwarded without mixing buffer and memory bytes, so it waits for the
    // store to drain (BUG-12, the SLUB corruption during boot).
    FwdResult forward_load(uint64_t load_addr, int load_size, uint64_t& fwd_data) {
        for (std::size_t k = 0; k < spec_count; k++) {
            std::size_t i = (spec_tail + SPEC_DEPTH - 1 - k) % SPEC_DEPTH;
            if (spec[i].valid) {
                uint64_t store_addr = spec[i].address;
                int store_size = spec[i].size;

                uint64_t overlap_start = std::max(load_addr, store_addr);
                uint64_t overlap_end = std::min(load_addr + load_size, store_addr + store_size);

                if (overlap_start < overlap_end) {
                    if (load_addr >= store_addr && (load_addr + load_size) <= (store_addr + store_size)) {
                        int offset = load_addr - store_addr;
                        uint64_t shifted = spec[i].data >> (offset * 8);
                        uint64_t mask = (load_size == 8) ? ~0ULL : ((1ULL << (load_size * 8)) - 1);
                        fwd_data = shifted & mask;
                        return FwdResult::HIT;
                    }
                    return FwdResult::PARTIAL_OVERLAP;
                }
            }
        }
        for (std::size_t k = 0; k < commit_count; k++) {
            std::size_t i = (commit_tail + COMMIT_DEPTH - 1 - k) % COMMIT_DEPTH;
            if (committed[i].valid) {
                uint64_t store_addr = committed[i].address;
                int store_size = committed[i].size;

                uint64_t overlap_start = std::max(load_addr, store_addr);
                uint64_t overlap_end = std::min(load_addr + load_size, store_addr + store_size);

                if (overlap_start < overlap_end) {
                    if (load_addr >= store_addr && (load_addr + load_size) <= (store_addr + store_size)) {
                        int offset = load_addr - store_addr;
                        uint64_t shifted = committed[i].data >> (offset * 8);
                        uint64_t mask = (load_size == 8) ? ~0ULL : ((1ULL << (load_size * 8)) - 1);
                        fwd_data = shifted & mask;
                        return FwdResult::HIT;
                    }
                    return FwdResult::PARTIAL_OVERLAP;
                }
            }
        }
        return FwdResult::MISS;
    }

    // AMOs and SC write memory directly, past this buffer. Drop any buffered
    // store they overlap, otherwise a later load would forward the old value.
    void invalidate_address(uint64_t addr, int size) {
        uint64_t end = addr + static_cast<uint64_t>(size);
        for (auto& e : spec) {
            if (e.valid) {
                uint64_t e_end = e.address + static_cast<uint64_t>(e.size);
                if (addr < e_end && end > e.address) e.valid = false;
            }
        }
        for (auto& e : committed) {
            if (e.valid) {
                uint64_t e_end = e.address + static_cast<uint64_t>(e.size);
                if (addr < e_end && end > e.address) e.valid = false;
            }
        }
    }

    bool spec_is_full()   const { return spec_count   == SPEC_DEPTH;   }
    bool commit_is_full() const { return commit_count  == COMMIT_DEPTH; }
    bool is_full()        const { return spec_is_full(); }
    bool is_empty()       const { return spec_count == 0 && commit_count == 0; }

    void flush_speculative() {
        for (auto& e : spec) e.valid = false;
        spec_head = spec_tail = spec_count = 0;
    }

    // Drop speculative stores from the tail whose scoreboard entry was flushed.
    template<std::size_t SB_SIZE>
    void flush_speculative(int branch_rob_idx, const Scoreboard<SB_SIZE>& scoreboard) {
        while (spec_count > 0) {
            std::size_t prev = (spec_tail + SPEC_DEPTH - 1) % SPEC_DEPTH;
            int tid = spec[prev].trans_id;
            if (tid >= 0 && !scoreboard.get_entry(tid).valid) {
                spec[prev].valid = false;
                spec_tail = prev;
                spec_count--;
            } else {
                break;
            }
        }
    }

    void flush_all() {
        flush_speculative();
        for (auto& e : committed) e.valid = false;
        commit_head = commit_tail = commit_count = 0;
    }

    void flush() { flush_all(); }

private:
    std::array<StoreBufferEntry, SPEC_DEPTH>   spec{};
    std::size_t spec_head{0}, spec_tail{0}, spec_count{0};

    std::array<StoreBufferEntry, COMMIT_DEPTH> committed{};
    std::size_t commit_head{0}, commit_tail{0}, commit_count{0};
    // CVA6 (store_buffer.sv) compares only address bits [11:3] and makes the
    // load wait for the drain on a match; this model compares byte ranges and
    // forwards instead (paper Section IV-G).
};

} // namespace riscv_tlm

#endif // STOREBUFFER_H
