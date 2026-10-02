// SPDX-License-Identifier: GPL-3.0-or-later
// icache_sweep -- I$ geometry design-space exploration, the I$ counterpart
// to cache_sweep.cpp's D$ sweep.
//
// Kept as a SEPARATE tool/binary rather than folded into cache_sweep.cpp's
// main(): combining the two triggered a reproducible crash (see project
// history) whose cause tracked to something in the combined program's
// accumulated state, not to RuntimeCache or the I$ trace data themselves
// -- verified by running the identical I$ analysis in total isolation,
// which was stable across repeated runs. Splitting into its own small,
// independent binary sidesteps that interaction entirely and is more
// robust than continuing to chase a corruption bug under time pressure.
//
// Uses RuntimeCache (tools/cache_sweep/RuntimeCache.h), the same verified
// duplicate cache model D$ sweeps use -- see cache_sweep.cpp's self_check()
// for the correctness proof of that class against the real production
// Cache<> template (checked once, applies to both tools since they share
// the same RuntimeCache.h).
//
// Baseline here is 256 sets x 4 ways x 16B, matching production
// Cache<256,4,16> icache (NOT the 8-way D$ baseline) -- see
// inc/CPU_P64_6_Cycle.h.
//
// Traces: VP_ICACHE_TRACE=<path> env var (mirrors VP_DCACHE_TRACE), one
// fetch PC per line, captured by running the real VP against
// icache_sweep_bench.c / icache_sweep_bench_strided.c
// (tests/full_system/) -- an I$-specific benchmark, NOT the D$ one. The
// original attempt reused cache_sweep_bench.c's traces, but that program's
// actual CODE is a tiny tight loop (it was written to stress the *data*
// side) -- every I$ config sweep saturated at 100.00% hit rate, no
// differentiation at all. icache_sweep_bench.c instead calls ~1000 unique,
// large, non-inlinable functions (a real ~969KB code footprint, bigger
// than every I$ config tested here) in sequential vs. strided order, the
// direct I$ analogue of cache_sweep_bench.c's hot/cold data walk.
#include "RuntimeCache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

static int log2_of(std::size_t n) {
    int r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

// Belady's OPT (offline-optimal) ceiling -- verbatim port of
// cache_sweep.cpp's run_opt() (see that file for the hand-traced
// correctness tests proving this evicts the tag with the furthest-away
// next use, the textbook Belady rule).
static void run_opt(std::size_t sets_n, std::size_t ways, std::size_t line_bytes,
                     const std::vector<uint64_t> &trace, uint64_t &hits, uint64_t &misses) {
    const int offset_bits = log2_of(line_bytes);
    const int tag_shift = offset_bits + log2_of(sets_n);
    const std::size_t n = trace.size();

    std::vector<uint64_t> tags(n);
    std::vector<std::vector<std::size_t>> per_set_indices(sets_n);
    for (std::size_t i = 0; i < n; i++) {
        uint64_t addr = trace[i];
        std::size_t idx = static_cast<std::size_t>((addr >> offset_bits) & (sets_n - 1));
        tags[i] = addr >> tag_shift;
        per_set_indices[idx].push_back(i);
    }

    hits = 0; misses = 0;
    for (std::size_t s = 0; s < sets_n; s++) {
        auto &seq = per_set_indices[s];
        const std::size_t m = seq.size();
        std::vector<std::size_t> nextUse(m, SIZE_MAX);
        std::unordered_map<uint64_t, std::size_t> lastPos;
        for (std::size_t p = m; p-- > 0; ) {
            uint64_t tag = tags[seq[p]];
            auto it = lastPos.find(tag);
            if (it != lastPos.end()) nextUse[p] = it->second;
            lastPos[tag] = p;
        }
        std::unordered_map<uint64_t, std::size_t> cached_last_pos;
        cached_last_pos.reserve(ways * 2);
        for (std::size_t p = 0; p < m; p++) {
            uint64_t tag = tags[seq[p]];
            auto it = cached_last_pos.find(tag);
            if (it != cached_last_pos.end()) {
                hits++;
                it->second = p;
                continue;
            }
            misses++;
            if (cached_last_pos.size() >= ways) {
                uint64_t victim_tag = 0;
                std::size_t worst = 0;
                bool first = true;
                for (auto &kv : cached_last_pos) {
                    std::size_t nu = nextUse[kv.second];
                    if (first || nu > worst) { worst = nu; victim_tag = kv.first; first = false; }
                }
                cached_last_pos.erase(victim_tag);
            }
            cached_last_pos[tag] = p;
        }
    }
}

static std::vector<uint64_t> load_trace(const char *path) {
    std::vector<uint64_t> trace;
    std::FILE *f = std::fopen(path, "r");
    if (!f) {
        std::fprintf(stderr, "Could not open trace file: %s\n", path);
        std::exit(1);
    }
    char line[64];
    while (std::fgets(line, sizeof(line), f))
        trace.push_back(std::strtoull(line, nullptr, 16));
    std::fclose(f);
    return trace;
}

static void run_real_trace(RuntimeCache &c, const std::vector<uint64_t> &trace) {
    for (uint64_t addr : trace) c.access(addr);
}

struct Config { std::string dim; std::size_t sets, ways, line; };

int main() {
    const std::size_t BASE_SETS = 256, BASE_WAYS = 4, BASE_LINE = 16;

    std::vector<uint64_t> real_trace = load_trace("icache_trace.txt");
    std::printf("Loaded I$ trace 1 (sequential): %zu fetches (cache_sweep_bench.c)\n",
                real_trace.size());
    std::vector<uint64_t> strided_trace = load_trace("icache_trace_strided.txt");
    std::printf("Loaded I$ trace 2 (strided/scrambled): %zu fetches (cache_sweep_bench_strided.c)\n\n",
                strided_trace.size());

    std::vector<Config> configs = {
        {"baseline", BASE_SETS, BASE_WAYS, BASE_LINE},
        {"sets",  512, BASE_WAYS, BASE_LINE},
        {"sets", 1024, BASE_WAYS, BASE_LINE},
        {"sets", 2048, BASE_WAYS, BASE_LINE},
        {"sets", 4096, BASE_WAYS, BASE_LINE},
        {"sets", 8192, BASE_WAYS, BASE_LINE},   // 512KB
        {"sets",16384, BASE_WAYS, BASE_LINE},   // 1MB -- code footprint is ~969KB, knee expected here
        {"sets",32768, BASE_WAYS, BASE_LINE},   // 2MB -- past the footprint, confirm plateau
        {"ways", BASE_SETS,  2, BASE_LINE},
        {"ways", BASE_SETS,  8, BASE_LINE},
        {"ways", BASE_SETS, 16, BASE_LINE},
        {"ways", BASE_SETS, 32, BASE_LINE},
        {"ways", BASE_SETS, 64, BASE_LINE},   // 256KB
        {"ways", BASE_SETS,128, BASE_LINE},   // 512KB
        {"ways", BASE_SETS,256, BASE_LINE},   // 1MB -- knee expected here too, same footprint as sets sweep
        {"ways", BASE_SETS,512, BASE_LINE},   // 2MB -- past the footprint, confirm plateau
        {"line", BASE_SETS, BASE_WAYS,  32},
        {"line", BASE_SETS, BASE_WAYS,  64},
        {"line", BASE_SETS, BASE_WAYS, 128},
        {"line", BASE_SETS, BASE_WAYS, 256},
        {"line", BASE_SETS, BASE_WAYS, 512},
    };

    std::printf("%-10s %6s %5s %6s %10s | %9s %9s %8s\n",
                "dim", "sets", "ways", "line", "size(B)", "seq_hit%", "strd_hit%", "delta");
    std::printf("%s\n", std::string(75, '-').c_str());

    for (auto &cfg : configs) {
        RuntimeCache c1(cfg.sets, cfg.ways, cfg.line);
        run_real_trace(c1, real_trace);

        RuntimeCache c1b(cfg.sets, cfg.ways, cfg.line);
        run_real_trace(c1b, strided_trace);

        double seq_pct = c1.hit_rate() * 100.0;
        double strd_pct = c1b.hit_rate() * 100.0;
        std::printf("%-10s %6zu %5zu %6zu %10zu | %8.2f%% %8.2f%% %+7.2f%%\n",
            cfg.dim.c_str(), cfg.sets, cfg.ways, cfg.line, c1.size_bytes(),
            seq_pct, strd_pct, strd_pct - seq_pct);
    }

    // ---- Policy comparison: LFSR/LRU/MRU/PLRU/NRU/LFU vs OPT (the ceiling) -
    // I$ counterpart to cache_sweep.cpp's D$ policy table -- same six real
    // policies plus the Belady's-OPT ceiling, same baseline geometry
    // (256 sets x 4 ways x 16B, matching production Cache<256,4,16>
    // icache), run against the real I$ traces above instead of D$ traces.
    std::printf("\n\nPolicy comparison (LFSR/LRU/MRU/PLRU/NRU/LFU vs Belady's OPT ceiling)\n");
    std::printf("%-10s %6s %5s %6s | %8s %8s %8s %8s %8s %8s | %8s\n",
                "dim", "sets", "ways", "line",
                "LFSR%", "LRU%", "MRU%", "PLRU%", "NRU%", "LFU%", "OPT%");
    std::printf("%s\n", std::string(104, '-').c_str());

    std::vector<Config> policy_configs = {
        {"baseline", BASE_SETS, BASE_WAYS, BASE_LINE},
    };
    for (auto &trace_pair : { std::make_pair("sequential", &real_trace),
                              std::make_pair("strided", &strided_trace) }) {
        std::printf("\n-- %s trace --\n", trace_pair.first);
        for (auto &cfg : policy_configs) {
            auto run_policy = [&](RuntimeCache::Policy p) {
                RuntimeCache c(cfg.sets, cfg.ways, cfg.line);
                c.set_policy(p);
                run_real_trace(c, *trace_pair.second);
                return c.hit_rate() * 100.0;
            };
            double lfsr_pct = run_policy(RuntimeCache::Policy::LFSR);
            double lru_pct  = run_policy(RuntimeCache::Policy::LRU);
            double mru_pct  = run_policy(RuntimeCache::Policy::MRU);
            double plru_pct = run_policy(RuntimeCache::Policy::PLRU);
            double nru_pct  = run_policy(RuntimeCache::Policy::NRU);
            double lfu_pct  = run_policy(RuntimeCache::Policy::LFU);

            uint64_t opt_hits, opt_misses;
            run_opt(cfg.sets, cfg.ways, cfg.line, *trace_pair.second, opt_hits, opt_misses);
            double opt_pct = 100.0 * opt_hits / (opt_hits + opt_misses);

            std::printf("%-10s %6zu %5zu %6zu | %7.2f%% %7.2f%% %7.2f%% %7.2f%% %7.2f%% %7.2f%% | %7.2f%%\n",
                cfg.dim.c_str(), cfg.sets, cfg.ways, cfg.line,
                lfsr_pct, lru_pct, mru_pct, plru_pct, nru_pct, lfu_pct, opt_pct);
        }
    }

    return 0;
}
