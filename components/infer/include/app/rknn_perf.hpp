#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rknn_api.h"

namespace app {

void LogRknnPerfRun(rknn_context ctx, const char* model_name, int log_interval = 30);

struct RknnPerfSnapshot {
    std::string model;
    uint64_t count = 0;
    int64_t last_us = 0;
    int64_t min_us = 0;
    int64_t max_us = 0;
    double avg_us = 0.0;
};

// Snapshot of all accumulated per-model stats since process start.
std::vector<RknnPerfSnapshot> GetRknnPerfSnapshots();

}  // namespace app
