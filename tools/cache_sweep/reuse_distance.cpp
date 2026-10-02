// SPDX-License-Identifier: GPL-3.0-or-later
// reuse_distance -- classifies a real D$ address trace as "locality-type"
// vs "scan-type" by measuring reuse distance: for each access, how many
// OTHER accesses occurred since the same line was last touched.
//
//   Locality-type: a subset of lines (the hot set) has a small, tight
//   reuse-distance cluster -- they get revisited quickly, which is exactly
//   what LRU/LFU-family policies are built to exploit.
//   Scan-type: reuse distance is large and roughly uniform across the whole
//   trace -- nothing is revisited until a full pass completes, which is
//   exactly what makes LRU pathological and MRU/LFSR the safer choice.
//
// Standalone, ad hoc analysis tool -- not part of the permanent sweep suite.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string>

static std::vector<uint64_t> load_trace(const char *path) {
    std::vector<uint64_t> trace;
    std::FILE *f = std::fopen(path, "r");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    char line[64];
    while (std::fgets(line, sizeof(line), f))
        trace.push_back(std::strtoull(line, nullptr, 16));
    std::fclose(f);
    return trace;
}

int main(int argc, char **argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <trace file>\n", argv[0]); return 1; }
    std::vector<uint64_t> trace = load_trace(argv[1]);
    const std::size_t n = trace.size();

    // Reuse distance measured in CACHE LINES (16B), matching the baseline
    // D$ line size -- two addresses in the same 16B line count as the same
    // "thing" for reuse purposes.
    std::unordered_map<uint64_t, std::size_t> last_seen;
    std::vector<uint64_t> distances; // only for addresses that DO get reused

    for (std::size_t i = 0; i < n; i++) {
        uint64_t line = trace[i] >> 4; // 16B line
        auto it = last_seen.find(line);
        if (it != last_seen.end()) {
            distances.push_back(static_cast<uint64_t>(i - it->second));
        }
        last_seen[line] = i;
    }

    std::unordered_map<uint64_t, int> touch_count;
    for (uint64_t a : trace) touch_count[a >> 4]++;
    uint64_t touched_once = 0;
    for (auto &kv : touch_count) if (kv.second == 1) touched_once++;

    std::sort(distances.begin(), distances.end());
    auto pct = [&](double p) -> uint64_t {
        if (distances.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(p * (distances.size() - 1));
        return distances[idx];
    };

    std::printf("=== Reuse-distance analysis: %s ===\n", argv[1]);
    std::printf("Total accesses:        %zu\n", n);
    std::printf("Distinct lines touched: %llu\n", (unsigned long long)touch_count.size());
    std::printf("Lines touched exactly once (never reused): %llu (%.1f%% of distinct lines)\n",
                (unsigned long long)touched_once,
                100.0 * touched_once / touch_count.size());
    std::printf("Reused accesses (has a prior touch):        %zu (%.1f%% of all accesses)\n",
                distances.size(), 100.0 * distances.size() / n);
    if (!distances.empty()) {
        std::printf("Reuse distance (lines): p10=%llu  p50(median)=%llu  p90=%llu  max=%llu\n",
                    (unsigned long long)pct(0.10), (unsigned long long)pct(0.50),
                    (unsigned long long)pct(0.90), (unsigned long long)distances.back());
        // fraction of reused accesses with SMALL reuse distance (<256 lines
        // = fits in the 32KB/16B baseline D$ trivially) vs LARGE (>2048
        // lines = bigger than the whole 32KB baseline D$'s line count)
        uint64_t small = 0, large = 0;
        for (uint64_t d : distances) {
            if (d < 256) small++;
            if (d > 2048) large++;
        }
        std::printf("Reuse distance < 256 lines (tight/hot reuse):  %llu (%.1f%% of reused accesses)\n",
                    (unsigned long long)small, 100.0 * small / distances.size());
        std::printf("Reuse distance > 2048 lines (loose/scan-like reuse): %llu (%.1f%% of reused accesses)\n",
                    (unsigned long long)large, 100.0 * large / distances.size());
    }
    return 0;
}
