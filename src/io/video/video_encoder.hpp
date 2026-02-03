/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "video_export_options.hpp"
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace lfs::io::video {

    class VideoEncoderImpl;

    class VideoEncoder {
    public:
        VideoEncoder();
        ~VideoEncoder();

        VideoEncoder(const VideoEncoder&) = delete;
        VideoEncoder& operator=(const VideoEncoder&) = delete;
        VideoEncoder(VideoEncoder&&) noexcept;
        VideoEncoder& operator=(VideoEncoder&&) noexcept;

        // Initialize encoder with output path and options
        [[nodiscard]] std::expected<void, std::string> open(
            const std::filesystem::path& output_path,
            const VideoExportOptions& options);

        // Write RGBA frame from CPU memory
        [[nodiscard]] std::expected<void, std::string> writeFrame(
            std::span<const uint8_t> rgba_data,
            int width,
            int height);

        // Write RGBA frame directly from GPU memory (zero-copy path)
        // Uses NVENC hardware encoding if available, falls back to x264 with CUDA color conversion
        [[nodiscard]] std::expected<void, std::string> writeFrameGpu(
            const void* rgba_gpu_ptr,
            int width,
            int height,
            void* cuda_stream = nullptr);

        // Finalize and close video file
        [[nodiscard]] std::expected<void, std::string> close();

        [[nodiscard]] bool isOpen() const;

    private:
        std::unique_ptr<VideoEncoderImpl> impl_;
    };

} // namespace lfs::io::video
