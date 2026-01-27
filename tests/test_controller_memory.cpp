/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "training/components/ppisp.hpp"
#include "training/components/ppisp_controller.hpp"
#include <cuda_runtime.h>
#include <gtest/gtest.h>

using namespace lfs::core;
using namespace lfs::training;

namespace {

    size_t get_free_vram() {
        size_t free_bytes, total_bytes;
        cudaMemGetInfo(&free_bytes, &total_bytes);
        return free_bytes;
    }

    size_t get_used_vram() {
        size_t free_bytes, total_bytes;
        cudaMemGetInfo(&free_bytes, &total_bytes);
        return total_bytes - free_bytes;
    }

} // namespace

// Simulate the controller distillation loop to check for memory leaks
TEST(PPISPControllerMemoryTest, DistillationLoopNoLeak) {
    constexpr int NUM_CAMERAS = 10;
    constexpr int NUM_ITERATIONS = 1000;
    constexpr int IMAGE_H = 544;
    constexpr int IMAGE_W = 816;

    // Create PPISP and controllers
    PPISP ppisp(30000);
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        ppisp.register_frame(i, i);
    }
    ppisp.finalize();
    std::vector<std::unique_ptr<PPISPController>> controllers;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        controllers.push_back(std::make_unique<PPISPController>(5000));
    }
    PPISPController::preallocate_shared_buffers(IMAGE_H, IMAGE_W);

    // Warm up - do a few iterations to stabilize memory
    auto input = Tensor::uniform({1, 3, IMAGE_H, IMAGE_W}, 0.0f, 1.0f, Device::CUDA);
    for (int i = 0; i < 10; ++i) {
        int cam_idx = i % NUM_CAMERAS;
        auto pred = controllers[cam_idx]->predict(input, 1.0f);
        auto target = ppisp.get_params_for_frame(cam_idx);
        auto loss = controllers[cam_idx]->distillation_loss(pred, target);
        controllers[cam_idx]->compute_mse_gradient(pred, target);
        controllers[cam_idx]->backward(controllers[cam_idx]->get_mse_gradient());
        controllers[cam_idx]->optimizer_step();
        controllers[cam_idx]->zero_grad();
        controllers[cam_idx]->scheduler_step();
    }
    cudaDeviceSynchronize();

    // Record baseline VRAM
    const size_t baseline_vram = get_used_vram();
    std::cout << "Baseline VRAM: " << baseline_vram / (1024 * 1024) << " MB" << std::endl;

    // Run many iterations
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        int cam_idx = iter % NUM_CAMERAS;

        auto pred = controllers[cam_idx]->predict(input, 1.0f);
        auto target = ppisp.get_params_for_frame(cam_idx);
        auto loss = controllers[cam_idx]->distillation_loss(pred, target);
        controllers[cam_idx]->compute_mse_gradient(pred, target);
        controllers[cam_idx]->backward(controllers[cam_idx]->get_mse_gradient());
        controllers[cam_idx]->optimizer_step();
        controllers[cam_idx]->zero_grad();
        controllers[cam_idx]->scheduler_step();

        // Check VRAM every 100 iterations
        if ((iter + 1) % 100 == 0) {
            cudaDeviceSynchronize();
            size_t current_vram = get_used_vram();
            size_t delta = current_vram > baseline_vram ? current_vram - baseline_vram : 0;
            std::cout << "Iter " << (iter + 1) << ": VRAM=" << current_vram / (1024 * 1024) << " MB, delta="
                      << delta / (1024 * 1024) << " MB" << std::endl;
        }
    }

    cudaDeviceSynchronize();
    const size_t final_vram = get_used_vram();
    const size_t leak = final_vram > baseline_vram ? final_vram - baseline_vram : 0;
    std::cout << "Final VRAM: " << final_vram / (1024 * 1024) << " MB" << std::endl;
    std::cout << "Memory growth: " << leak / (1024 * 1024) << " MB over " << NUM_ITERATIONS << " iterations"
              << std::endl;

    // Allow up to 50MB growth (cache warming)
    EXPECT_LT(leak, 50 * 1024 * 1024) << "Memory leak detected: " << leak / (1024 * 1024) << " MB";
}

// Test with varying image sizes (simulates different cameras)
TEST(PPISPControllerMemoryTest, VaryingImageSizesNoLeak) {
    constexpr int NUM_ITERATIONS = 500;

    std::vector<std::pair<int, int>> sizes = {{544, 816}, {480, 640}, {720, 1280}, {600, 800}};

    PPISPController controller(5000);
    PPISP ppisp(500);
    for (int i = 0; i < 4; ++i) {
        ppisp.register_frame(i, i);
    }
    ppisp.finalize();
    PPISPController::preallocate_shared_buffers(720, 1280);

    // Warm up
    for (int i = 0; i < 20; ++i) {
        auto [h, w] = sizes[i % sizes.size()];
        auto input = Tensor::uniform({1, 3, static_cast<size_t>(h), static_cast<size_t>(w)}, 0.0f, 1.0f, Device::CUDA);
        auto pred = controller.predict(input, 1.0f);
        auto target = ppisp.get_params_for_frame(0);
        controller.compute_mse_gradient(pred, target);
        controller.backward(controller.get_mse_gradient());
        controller.optimizer_step();
        controller.zero_grad();
    }
    cudaDeviceSynchronize();

    const size_t baseline_vram = get_used_vram();
    std::cout << "Baseline VRAM (varying sizes): " << baseline_vram / (1024 * 1024) << " MB" << std::endl;

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        auto [h, w] = sizes[iter % sizes.size()];
        auto input = Tensor::uniform({1, 3, static_cast<size_t>(h), static_cast<size_t>(w)}, 0.0f, 1.0f, Device::CUDA);
        auto pred = controller.predict(input, 1.0f);
        auto target = ppisp.get_params_for_frame(0);
        controller.compute_mse_gradient(pred, target);
        controller.backward(controller.get_mse_gradient());
        controller.optimizer_step();
        controller.zero_grad();
    }

    cudaDeviceSynchronize();
    const size_t final_vram = get_used_vram();
    const size_t leak = final_vram > baseline_vram ? final_vram - baseline_vram : 0;
    std::cout << "Memory growth (varying sizes): " << leak / (1024 * 1024) << " MB" << std::endl;

    // Varying sizes will cause more cache growth, allow 100MB
    EXPECT_LT(leak, 100 * 1024 * 1024) << "Memory leak detected: " << leak / (1024 * 1024) << " MB";
}
