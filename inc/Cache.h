// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef CACHE_H
#define CACHE_H

#include <cstdint>
#include <array>

namespace riscv_tlm {

// Set-associative cache, tags only (no data). Replacement defaults to LFSR
// random like the CVA6 L1s (cva6_icache.sv, wt_dcache_missunit.sv): fill an
// invalid way if there is one, otherwise evict the way an 8-bit LFSR picks.
// We had LRU before; on scans larger than the cache it evicts the line about
// to be reused and over-missed by about 22% (BUGS.md B8).
//
// LRU, MRU, PLRU, NRU and LFU can be selected for comparison, but CVA6 only
// has LFSR. When comparing, don't use only large sequential scans: they are
// LRU's worst case. Use workloads with a hot set that gets reused.
//
// SETS, WAYS and LINE_BYTES must be powers of 2. The defaults below are
// placeholders; the CPU uses Cache<256,4,16> and Cache<256,8,16>.
template<std::size_t SETS = 64, std::size_t WAYS = 4, std::size_t LINE_BYTES = 64>
class Cache {
public:
    // Line size for code outside the template (AXI burst length).
    static constexpr std::size_t LineBytes = LINE_BYTES;

    // Everything except LFSR is for comparison. MRU, PLRU, NRU and LFU come
    // from tools/cache_sweep/RuntimeCache.h, which has the tests.
    enum class Policy { LFSR, LRU, MRU, PLRU, NRU, LFU };

    Cache() = default;

    // Selects the eviction policy. Default (LFSR) matches CVA6 RTL; LRU is
    // provided for comparison only. Safe to call before or between runs
    // (does not itself touch cache state -- call flush() too if you want a
    // clean start after switching mid-run).
    void set_policy(Policy p) { policy_ = p; }
    Policy get_policy() const { return policy_; }

    // Returns true on hit, false on miss.
    // On miss: fills an invalid way, else evicts per the active policy.
    bool access(uint64_t addr) {
        const uint64_t idx = (addr >> OFFSET_BITS) & (SETS - 1);
        const uint64_t tag = addr >> TAG_SHIFT;
        total_accesses++;

        auto& s = sets[idx];

        for (std::size_t w = 0; w < WAYS; w++) {
            auto& line = s[w];
            if (line.valid && line.tag == tag) {
                line.lru_time = total_accesses; // only consulted in LRU/MRU mode
                line.ref_bit = true;            // only consulted in NRU mode
                line.freq++;                    // only consulted in LFU mode
                if (policy_ == Policy::PLRU) plru_mark_mru(idx, w);
                total_hits++;
                return true;
            }
        }

        // Miss: fill the first invalid way; if all ways are valid, evict
        // per policy.
        std::size_t victim = WAYS;
        for (std::size_t w = 0; w < WAYS; w++) {
            if (!s[w].valid) { victim = w; break; }
        }
        if (victim == WAYS) {
            if (policy_ == Policy::LRU) {
                victim = 0;
                for (std::size_t w = 1; w < WAYS; w++)
                    if (s[w].lru_time < s[victim].lru_time) victim = w;
            } else if (policy_ == Policy::MRU) {
                // Mirror image of LRU: evict the MOST recently touched way.
                victim = 0;
                for (std::size_t w = 1; w < WAYS; w++)
                    if (s[w].lru_time > s[victim].lru_time) victim = w;
            } else if (policy_ == Policy::PLRU) {
                victim = plru_find_victim(idx);
            } else if (policy_ == Policy::NRU) {
                victim = WAYS;
                for (std::size_t w = 0; w < WAYS; w++)
                    if (!s[w].ref_bit) { victim = w; break; }
                if (victim == WAYS) {
                    for (std::size_t w = 0; w < WAYS; w++) s[w].ref_bit = false;
                    victim = 0;
                }
            } else if (policy_ == Policy::LFU) {
                victim = 0;
                for (std::size_t w = 1; w < WAYS; w++)
                    if (s[w].freq < s[victim].freq) victim = w;
            } else {
                // LFSR (mirrors CVA6's update_lfsr = cache_wren & all_ways_valid).
                victim = static_cast<std::size_t>(lfsr) % WAYS;   // rnd_way
                // 8-bit Fibonacci LFSR, taps x^8+x^6+x^5+x^4+1 (maximal length).
                const uint8_t fb = ((lfsr >> 7) ^ (lfsr >> 5) ^ (lfsr >> 4) ^ (lfsr >> 3)) & 1u;
                lfsr = static_cast<uint8_t>((lfsr << 1) | fb);
            }
        }
        s[victim] = {true, tag, total_accesses, true, 1};
        if (policy_ == Policy::PLRU) plru_mark_mru(idx, victim);
        return false;
    }

    void flush() {
        for (auto& s : sets)
            for (auto& line : s)
                line.valid = false;
    }

    uint64_t get_accesses() const { return total_accesses; }
    uint64_t get_hits()     const { return total_hits; }
    uint64_t get_misses()   const { return total_accesses - total_hits; }
    double   hit_rate()     const {
        return total_accesses
            ? static_cast<double>(total_hits) / static_cast<double>(total_accesses)
            : 0.0;
    }

private:
    struct Line {
        bool     valid{false};
        uint64_t tag{0};
        uint64_t lru_time{0}; // only maintained/consulted in LRU/MRU mode
        bool     ref_bit{false}; // only maintained/consulted in NRU mode
        uint64_t freq{0};        // only maintained/consulted in LFU mode
    };

    // Tree-PLRU helpers (WAYS is asserted power-of-2 elsewhere in this
    // project's Cache<> usages; a complete binary tree of WAYS-1 one-bit
    // nodes covers exactly WAYS leaves). Bit convention: bits[node]==0
    // means "descend left on eviction" (the right subtree was more
    // recently touched), ==1 means "descend right". Ported verbatim from
    // tools/cache_sweep/RuntimeCache.h (see that file for the hand-traced
    // correctness proof).
    // log2c must be declared before its first use in a static member
    // initialiser (GCC 15 and Clang reject a forward use).
    static constexpr int log2c(std::size_t n) {
        return (n <= 1) ? 0 : 1 + log2c(n >> 1);
    }
    static constexpr int WAYS_LOG2 = log2c(WAYS);
    std::size_t plru_find_victim(std::size_t set_idx) const {
        std::size_t node = 0;
        auto& bits = plru_bits[set_idx];
        for (int level = 0; level < WAYS_LOG2; level++)
            node = 2 * node + 1 + bits[node];
        return node - (WAYS - 1);
    }
    void plru_mark_mru(std::size_t set_idx, std::size_t way) {
        auto& bits = plru_bits[set_idx];
        std::size_t node = 0;
        for (int level = WAYS_LOG2 - 1; level >= 0; level--) {
            unsigned dir = (way >> level) & 1u;
            bits[node] = static_cast<uint8_t>(1u - dir);
            node = 2 * node + 1 + dir;
        }
    }

    Policy policy_{Policy::LFSR}; // default matches CVA6 RTL

    // Single 8-bit LFSR shared across all sets, as in CVA6 (one lfsr instance
    // per cache). Nonzero seed (an LFSR must never be all-zero).
    uint8_t lfsr{0xACu};

    static constexpr int OFFSET_BITS = log2c(LINE_BYTES);
    static constexpr int TAG_SHIFT   = log2c(LINE_BYTES) + log2c(SETS);

    std::array<std::array<Line, WAYS>, SETS> sets{};
    std::array<std::array<uint8_t, (WAYS > 1 ? WAYS - 1 : 1)>, SETS> plru_bits{};
    uint64_t total_accesses{0};
    uint64_t total_hits{0};
};

} // namespace riscv_tlm

#endif // CACHE_H
