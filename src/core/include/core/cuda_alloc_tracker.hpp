/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/export.hpp"

#include <cstddef>
#include <cuda_runtime.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lfs::core {

    class LFS_CORE_API CudaAllocTracker {
    public:
        static CudaAllocTracker& instance();

        void record_alloc(void* ptr, size_t bytes, const char* location);
        void record_free(void* ptr);
        void print_summary();

    private:
        struct AllocInfo {
            size_t bytes;
            std::string location;
        };

        CudaAllocTracker() = default;

        std::mutex mutex_;
        std::unordered_map<void*, AllocInfo> allocations_;
        size_t total_allocated_ = 0;
        size_t total_freed_ = 0;
        double last_print_gb_ = 0;
    };

#define TRACKED_CUDA_MALLOC(ptr, size, location)                                          \
    do {                                                                                  \
        cudaError_t err = cudaMalloc(ptr, size);                                          \
        if (err == cudaSuccess) {                                                         \
            lfs::core::CudaAllocTracker::instance().record_alloc(*(ptr), size, location); \
        }                                                                                 \
    } while (0)

#define TRACKED_CUDA_FREE(ptr)                                    \
    do {                                                          \
        lfs::core::CudaAllocTracker::instance().record_free(ptr); \
        cudaFree(ptr);                                            \
    } while (0)

} // namespace lfs::core
