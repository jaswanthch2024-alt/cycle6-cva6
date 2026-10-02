// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef CACHE_H
#define CACHE_H

#include <cstdint>
#include <array>

namespace riscv_tlm {

// N-way set-associative cache with a SELECTABLE replacement policy.
//
// Default is LFSR pseudo-random, matching the CVA6 L1 caches (cva6_icache.sv
// and wt_dcache_missunit.sv): on a miss, fill an invalid way if one exists,
// otherwise evict a way chosen by an 8-bit LFSR. This is the policy real CVA6
// silicon implements, and is what every RTL-accuracy comparison in this
// project uses — see docs/BUGS.md §B8 (LRU was pathologically bad on
// sequential scans larger than the cache: it evicts exactly the line about to
// be reused, which made the VP over-miss by ~22% vs CVA6 on memory-bound
// code).
//
// LRU is also implemented and selectable via set_policy(), but ONLY for
// side-by-side policy comparison on your own workloads -- it is NOT a
// second CVA6-accurate mode (real CVA6 never uses LRU). Do not use LRU
// results as an RTL-accuracy claim.
//
// IMPORTANT when comparing LRU vs LFSR: the benchmarks built during the §B8
// investigation (large sequential scans of a working set > cache size) are
// LRU's textbook WORST case and LFSR's best case -- testing only with those
// is circular, since that access pattern is exactly why LFSR was adopted.
// A fair comparison needs workloads with genuine temporal locality (a hot
// working set reused many times, ideally close to or under cache capacity),
// where LRU's "recently used = likely reused soon" assumption actually holds.
//
// SETS, WAYS and LINE_BYTES must be powers of 2.
// Default: 64 sets × 4 ways × 64B lines = 16 KB (illustrative; real
// instances are Cache<256,4,16> / Cache<256,8,16>).
template<std::size_t SETS = 64, std::size_t WAYS = 4, std::size_t LINE_BYTES = 64>
class Cache {
public:
    // Exposes the line size to callers outside the template (e.g. the AXI
    // refill-burst-length calculation), since a non-type template parameter
    // isn't otherwise nameable as Cache<...>::LINE_BYTES from outside.
    static constexpr std::size_t LineBytes = LINE_BYTES;

    // MRU, PLRU, NRU, LFU are comparison-only additions (like LRU above,
    // NOT a second CVA6-accurate mode) -- ported from the verified
    // tools/cache_sweep/RuntimeCache.h implementations (see that file's
    // header comment for per-policy motivation and its hand-traced
    // correctness tests) so VP_CACHE_POLICY comparisons can run on the
    // real, cycle-accurate pipeline instead of only the standalone
    // hit-rate exploration tool.
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
    static constexpr int WAYS_LOG2 = Cache::log2c(WAYS);
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

    static constexpr int log2c(std::size_t n) {
        return (n <= 1) ? 0 : 1 + log2c(n >> 1);
    }
    static constexpr int OFFSET_BITS = log2c(LINE_BYTES);
    static constexpr int TAG_SHIFT   = log2c(LINE_BYTES) + log2c(SETS);

    std::array<std::array<Line, WAYS>, SETS> sets{};
    std::array<std::array<uint8_t, (WAYS > 1 ? WAYS - 1 : 1)>, SETS> plru_bits{};
    uint64_t total_accesses{0};
    uint64_t total_hits{0};
};

} // namespace riscv_tlm

#endif // CACHE_H
