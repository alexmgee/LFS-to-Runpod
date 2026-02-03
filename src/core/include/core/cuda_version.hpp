/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/export.hpp"
#include <string>

namespace lfs::core {

    // CUDA 12.8 minimum (NVIDIA driver 570+)
    // Format: major * 1000 + minor * 10
    constexpr int MIN_CUDA_VERSION = 12080;

    struct CudaVersionInfo {
        int driver_version = 0;
        int major = 0;
        int minor = 0;
        bool supported = false;
        bool query_failed = false;
    };

    LFS_CORE_API CudaVersionInfo check_cuda_version();

    // PyTorch CUDA wheel tags
    enum class PytorchCudaTag {
        CU118, // CUDA 11.8
        CU121, // CUDA 12.1
        CU124, // CUDA 12.4
        CU128  // CUDA 12.8
    };

    // Get the PyTorch CUDA wheel tag for package installation
    // If version_hint is "auto", detects from system CUDA driver
    // If version_hint is "11.8", "12.1", "12.4", "12.8", uses that version
    // Returns tag string like "cu118", "cu121", "cu124", "cu128"
    LFS_CORE_API std::string get_pytorch_cuda_tag(const std::string& version_hint = "auto");

    // Convert tag enum to string (e.g., CU124 -> "cu124")
    constexpr const char* pytorch_cuda_tag_str(PytorchCudaTag tag) {
        switch (tag) {
        case PytorchCudaTag::CU118: return "cu118";
        case PytorchCudaTag::CU121: return "cu121";
        case PytorchCudaTag::CU124: return "cu124";
        case PytorchCudaTag::CU128: return "cu128";
        }
        return "cu128";
    }

} // namespace lfs::core
