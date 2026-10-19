#include "app/rknn_perf.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace app {
namespace {

struct PerfStats {
    uint64_t count = 0;
    uint64_t fail_count = 0;
    int64_t last_us = 0;
    int64_t min_us = std::numeric_limits<int64_t>::max();
    int64_t max_us = 0;
    long double total_us = 0.0L;
};

struct PerfSnapshot {
    uint64_t count = 0;
    int64_t last_us = 0;
    int64_t min_us = 0;
    int64_t max_us = 0;
    long double avg_us = 0.0L;
};

std::mutex& PerfMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, PerfStats>& PerfMap() {
    static std::unordered_map<std::string, PerfStats> stats;
    return stats;
}

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == '\n' || value[first] == '\r' || value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    if (first > 0) {
        value.erase(0, first);
    }
    return value;
}

bool ReadTextFile(const char* path, std::string* out) {
    if (!path || !out) {
        return false;
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = Trim(ss.str());
    return !out->empty();
}

std::string NpuStatusString() {
    std::string governor;
    std::string cur_freq;
    std::string devfreq_load;
    std::string debug_load;
    const bool has_governor = ReadTextFile("/sys/class/devfreq/fdab0000.npu/governor", &governor);
    const bool has_cur_freq = ReadTextFile("/sys/class/devfreq/fdab0000.npu/cur_freq", &cur_freq);
    const bool has_devfreq_load = ReadTextFile("/sys/class/devfreq/fdab0000.npu/load", &devfreq_load);
    const bool has_debug_load = ReadTextFile("/sys/kernel/debug/rknpu/load", &debug_load);

    std::ostringstream oss;
    if (has_governor) {
        oss << " npu_governor=" << governor;
    }
    if (has_cur_freq) {
        oss << " npu_cur_freq_hz=" << cur_freq;
    }
    if (has_devfreq_load) {
        oss << " npu_devfreq_load=" << devfreq_load;
    }
    if (has_debug_load) {
        oss << " npu_debug_load=\"" << debug_load << "\"";
    }
    return oss.str();
}

}  // namespace

void LogRknnPerfRun(rknn_context ctx, const char* model_name, int log_interval) {
    if (ctx == 0 || model_name == nullptr || model_name[0] == '\0') {
        return;
    }
    if (log_interval <= 0) {
        log_interval = 30;
    }

    rknn_perf_run perf{};
    int ret = rknn_query(ctx, RKNN_QUERY_PERF_RUN, &perf, sizeof(perf));
    const std::string key(model_name);

    std::lock_guard<std::mutex> lock(PerfMutex());
    PerfStats& stats = PerfMap()[key];
    if (ret != RKNN_SUCC) {
        ++stats.fail_count;
        if (stats.fail_count == 1 || stats.fail_count % static_cast<uint64_t>(log_interval) == 0) {
            std::cerr << "[RKNN_PERF][WARN] model=" << key << " rknn_query(RKNN_QUERY_PERF_RUN) failed: " << ret
                      << " fail_count=" << stats.fail_count << "\n";
        }
        return;
    }

    const int64_t run_us = perf.run_duration;
    ++stats.count;
    stats.last_us = run_us;
    stats.min_us = std::min(stats.min_us, run_us);
    stats.max_us = std::max(stats.max_us, run_us);
    stats.total_us += static_cast<long double>(run_us);
    // No per-call print: snapshot drained by GetRknnPerfSnapshots() from the heartbeat path.
}

std::vector<RknnPerfSnapshot> GetRknnPerfSnapshots() {
    std::vector<RknnPerfSnapshot> out;
    std::lock_guard<std::mutex> lock(PerfMutex());
    out.reserve(PerfMap().size());
    for (const auto& [key, stats] : PerfMap()) {
        if (stats.count == 0) continue;
        RknnPerfSnapshot snap;
        snap.model = key;
        snap.count = stats.count;
        snap.last_us = stats.last_us;
        snap.min_us = stats.min_us;
        snap.max_us = stats.max_us;
        snap.avg_us = static_cast<double>(stats.total_us / static_cast<long double>(stats.count));
        out.push_back(std::move(snap));
    }
    return out;
}

}  // namespace app
