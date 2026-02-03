/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "core/cuda_alloc_tracker.hpp"

#include <algorithm>
#include <cstdio>

namespace lfs::core {

    CudaAllocTracker& CudaAllocTracker::instance() {
        static CudaAllocTracker tracker;
        return tracker;
    }

    void CudaAllocTracker::record_alloc(void* ptr, size_t bytes, const char* location) {
        if (!ptr)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        allocations_[ptr] = {bytes, location ? location : "unknown"};
        total_allocated_ += bytes;

        if (total_allocated_ / (1024 * 1024 * 1024.0) > last_print_gb_ + 0.5) {
            last_print_gb_ = total_allocated_ / (1024 * 1024 * 1024.0);
            print_summary();
        }
    }

    void CudaAllocTracker::record_free(void* ptr) {
        if (!ptr)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            total_freed_ += it->second.bytes;
            allocations_.erase(it);
        }
    }

    void CudaAllocTracker::print_summary() {
        std::lock_guard<std::mutex> lock(mutex_);

        printf("\n========== CUDA ALLOCATION TRACKER ==========\n");
        printf("Total allocated: %.2f GB\n", total_allocated_ / (1024.0 * 1024 * 1024));
        printf("Total freed: %.2f GB\n", total_freed_ / (1024.0 * 1024 * 1024));
        printf("Currently allocated: %.2f GB (%zu allocations)\n",
               (total_allocated_ - total_freed_) / (1024.0 * 1024 * 1024),
               allocations_.size());

        std::unordered_map<std::string, size_t> by_location;
        for (const auto& [ptr, info] : allocations_) {
            by_location[info.location] += info.bytes;
        }

        printf("\nTop allocations by location:\n");
        std::vector<std::pair<std::string, size_t>> sorted(by_location.begin(), by_location.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        for (size_t i = 0; i < std::min(size_t(10), sorted.size()); i++) {
            printf("  %60s: %.2f MB\n",
                   sorted[i].first.c_str(),
                   sorted[i].second / (1024.0 * 1024));
        }
        printf("=============================================\n\n");
    }

} // namespace lfs::core
