// SPDX-License-Identifier: GPL-3.0-or-later
// RuntimeCache -- a runtime-parameterized duplicate of inc/Cache.h's
// N-way set-associative cache model, for cache-geometry sweep experiments.
//
// Deliberately a SEPARATE class, not a modification of the production
// riscv_tlm::Cache<SETS,WAYS,LINE_BYTES> template used by the actual VP
// pipeline (inc/CPU_P64_6_Cycle.h) -- this keeps the RTL-validated production
// cache completely untouched. SETS/WAYS/LINE_BYTES are constructor
// arguments here instead of template parameters, so many geometries can be
// swept in one compiled binary with no rebuild between configs.
//
// Semantics are a deliberate 1:1 mirror of Cache.h's access()/policy logic
// (see cache_sweep.cpp's self-check, which runs both classes on an
// identical access sequence and asserts bit-identical hit/miss decisions
// at every step -- this is the correctness proof that this duplicate
// faithfully represents the real cache, not just something similar to it).
#pragma once
#include <cstdint>
#include <cassert>
#include <vector>

class RuntimeCache {
public:
    // MRU, PLRU, NRU, LFU are all exploration-only (not mirrored in the
    // production inc/Cache.h -- none are CVA6-relevant options, unlike LRU
    // which exists there as the documented pre-B8 baseline).
    //   MRU  -- motivated directly by the LFSR-vs-LRU finding: LRU is
    //           pathological on a scan larger than the cache because it
    //           evicts exactly the line about to be reused -- MRU is the
    //           textbook fix for that specific case, at the cost of being a
    //           bad policy for anything with real temporal locality.
    //   PLRU -- tree-based pseudo-LRU, the real-hardware compromise: N-1
    //           one-bit nodes per set instead of full per-line timestamps,
    //           used in many real L1/L2 caches (e.g. several x86
    //           implementations) because true LRU's per-line counters don't
    //           scale to high associativity as cheaply in silicon.
    //   NRU  -- single reference-bit-per-line "second chance"/CLOCK-style
    //           policy: evict the first line with its bit clear, or clear
    //           every bit in the set and retry if none are clear. Common in
    //           TLBs and simple page-replacement schemes where a full
    //           recency order is more hardware than the structure is worth.
    //   LFU  -- evicts the line with the fewest total hits seen, not the
    //           least-recently-touched one. Common in web/object caches
    //           where popularity is a better long-run predictor than
    //           recency; included here mainly as a contrast case since it
    //           has no real per-line "time" concept at all.
    enum class Policy { LFSR, LRU, MRU, PLRU, NRU, LFU };

    RuntimeCache(std::size_t sets, std::size_t ways, std::size_t line_bytes)
        : sets_(sets), ways_(ways), line_bytes_(line_bytes),
          offset_bits_(log2c(line_bytes)),
          tag_shift_(log2c(line_bytes) + log2c(sets)),
          ways_log2_(log2c(ways)),
          lines_(sets, std::vector<Line>(ways)),
          plru_bits_(sets, std::vector<uint8_t>(ways > 1 ? ways - 1 : 0, 0)) {
        assert(is_pow2(sets) && is_pow2(ways) && is_pow2(line_bytes) &&
               "RuntimeCache: SETS, WAYS, LINE_BYTES must all be powers of 2");
    }

    void set_policy(Policy p) { policy_ = p; }
    Policy get_policy() const { return policy_; }

    bool access(uint64_t addr) {
        const uint64_t idx = (addr >> offset_bits_) & (sets_ - 1);
        const uint64_t tag = addr >> tag_shift_;
        total_accesses_++;

        auto &s = lines_[idx];
        for (std::size_t w = 0; w < ways_; w++) {
            auto &line = s[w];
            if (line.valid && line.tag == tag) {
                line.lru_time = total_accesses_;
                line.ref_bit = true;
                line.freq++;
                if (policy_ == Policy::PLRU) plru_mark_mru(idx, w);
                total_hits_++;
                return true;
            }
        }

        std::size_t victim = ways_;
        for (std::size_t w = 0; w < ways_; w++) {
            if (!s[w].valid) { victim = w; break; }
        }
        if (victim == ways_) {
            if (policy_ == Policy::LRU) {
                victim = 0;
                for (std::size_t w = 1; w < ways_; w++)
                    if (s[w].lru_time < s[victim].lru_time) victim = w;
            } else if (policy_ == Policy::MRU) {
                // Mirror image of LRU: evict the MOST recently touched way
                // instead of the least. Reuses the same lru_time bookkeeping.
                victim = 0;
                for (std::size_t w = 1; w < ways_; w++)
                    if (s[w].lru_time > s[victim].lru_time) victim = w;
            } else if (policy_ == Policy::PLRU) {
                victim = plru_find_victim(idx);
            } else if (policy_ == Policy::NRU) {
                victim = ways_;
                for (std::size_t w = 0; w < ways_; w++)
                    if (!s[w].ref_bit) { victim = w; break; }
                if (victim == ways_) {
                    for (std::size_t w = 0; w < ways_; w++) s[w].ref_bit = false;
                    victim = 0;
                }
            } else if (policy_ == Policy::LFU) {
                victim = 0;
                for (std::size_t w = 1; w < ways_; w++)
                    if (s[w].freq < s[victim].freq) victim = w;
            } else {
                victim = static_cast<std::size_t>(lfsr_) % ways_;
                const uint8_t fb = ((lfsr_ >> 7) ^ (lfsr_ >> 5) ^ (lfsr_ >> 4) ^ (lfsr_ >> 3)) & 1u;
                lfsr_ = static_cast<uint8_t>((lfsr_ << 1) | fb);
            }
        }
        s[victim] = {true, tag, total_accesses_, true, 1};
        if (policy_ == Policy::PLRU) plru_mark_mru(idx, victim);
        return false;
    }

    void flush() {
        for (auto &s : lines_)
            for (auto &line : s)
                line.valid = false;
    }

    std::size_t sets() const { return sets_; }
    std::size_t ways() const { return ways_; }
    std::size_t line_bytes() const { return line_bytes_; }
    std::size_t size_bytes() const { return sets_ * ways_ * line_bytes_; }

    uint64_t get_accesses() const { return total_accesses_; }
    uint64_t get_hits()     const { return total_hits_; }
    uint64_t get_misses()   const { return total_accesses_ - total_hits_; }
    double   hit_rate()     const {
        return total_accesses_ ? static_cast<double>(total_hits_) / static_cast<double>(total_accesses_) : 0.0;
    }

private:
    struct Line {
        bool     valid{false};
        uint64_t tag{0};
        uint64_t lru_time{0};
        bool     ref_bit{false}; // NRU
        uint64_t freq{0};        // LFU
    };

    // Tree-PLRU helpers (ways_ is asserted power-of-2, so a complete binary
    // tree of ways_-1 one-bit nodes covers exactly ways_ leaves). Bit
    // convention: bits[node]==0 means "descend left on eviction" (i.e. the
    // right subtree was more recently touched), ==1 means "descend right".
    std::size_t plru_find_victim(std::size_t set_idx) const {
        std::size_t node = 0;
        auto &bits = plru_bits_[set_idx];
        for (int level = 0; level < ways_log2_; level++)
            node = 2 * node + 1 + bits[node];
        return node - (ways_ - 1);
    }
    void plru_mark_mru(std::size_t set_idx, std::size_t way) {
        auto &bits = plru_bits_[set_idx];
        std::size_t node = 0;
        for (int level = ways_log2_ - 1; level >= 0; level--) {
            unsigned dir = (way >> level) & 1u;
            bits[node] = static_cast<uint8_t>(1u - dir); // point away from the side just used
            node = 2 * node + 1 + dir;
        }
    }

    static bool is_pow2(std::size_t n) { return n && ((n & (n - 1)) == 0); }
    static int log2c(std::size_t n) {
        int r = 0;
        while (n > 1) { n >>= 1; r++; }
        return r;
    }

    std::size_t sets_, ways_, line_bytes_;
    int offset_bits_, tag_shift_, ways_log2_;
    std::vector<std::vector<Line>> lines_;
    std::vector<std::vector<uint8_t>> plru_bits_;

    Policy  policy_{Policy::LFSR};
    uint8_t lfsr_{0xACu};
    uint64_t total_accesses_{0};
    uint64_t total_hits_{0};
};
