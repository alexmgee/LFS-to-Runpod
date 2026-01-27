/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include "core/tensor.hpp"
#include "lfs/kernels/ppisp.cuh"
#include <cmath>
#include <istream>
#include <ostream>
#include <unordered_map>

namespace lfs::training {

    /// User-controllable overrides for PPISP rendering (matches paper Section 4 / Supplementary D)
    struct PPISPRenderOverrides {
        // Exposure (Section 4.1)
        float exposure_offset = 0.0f; // EV stops

        // Vignetting (Section 4.2)
        bool vignette_enabled = true;
        float vignette_strength = 1.0f;

        // Color Correction (Section 4.3) - 4 chromaticity control points
        // RGB primary offsets (Δc_R, Δc_G, Δc_B)
        float color_blue_x = 0.0f;
        float color_blue_y = 0.0f;
        float color_red_x = 0.0f;
        float color_red_y = 0.0f;
        float color_green_x = 0.0f;
        float color_green_y = 0.0f;
        // White/neutral point offset (Δc_W) - exposed as temperature/tint
        float wb_temperature = 0.0f;
        float wb_tint = 0.0f;

        // CRF (Section 4.4) - piecewise power curve
        float gamma_multiplier = 1.0f; // Overall gamma
        float gamma_red = 0.0f;        // Per-channel gamma offsets
        float gamma_green = 0.0f;
        float gamma_blue = 0.0f;
        float crf_toe = 0.0f;      // Shadow compression (τ adjustment)
        float crf_shoulder = 0.0f; // Highlight roll-off (η adjustment)
    };

    struct PPISPConfig {
        double lr = 2e-3;
        double beta1 = 0.9;
        double beta2 = 0.999;
        double eps = 1e-15;
        int warmup_steps = 500;
        double warmup_start_factor = 0.01;
        double final_lr_factor = 0.01;

        // Regularization weights (matching Python PPISP)
        float exposure_mean = 1.0f; // Encourage exposure mean ~ 0 (resolve SH ambiguity)
        float vig_center = 0.02f;   // Encourage vignetting optical center near image center
        float vig_channel = 0.1f;   // Encourage similar vignetting across RGB channels
        float vig_non_pos = 0.01f;  // Penalize positive vignetting alpha coefficients
        float color_mean = 1.0f;    // Encourage color correction mean ~ 0
        float crf_channel = 0.1f;   // Encourage similar CRF across RGB channels
    };

    /// Physically-Plausible Image Signal Processing for per-camera/per-frame appearance modeling
    class PPISP {
    public:
        using Config = PPISPConfig;

        explicit PPISP(int total_iterations, Config config = {});

        /// Register a frame with its camera assignment (call before finalize)
        void register_frame(int uid, int camera_id);

        /// Finalize registration and allocate tensors (must call after all register_frame calls)
        void finalize();

        /// Check if a frame UID is registered
        bool is_known_frame(int uid) const;

        /// Check if a camera ID is registered
        bool is_known_camera(int camera_id) const;

        /// Get the camera ID for a registered frame
        int camera_for_frame(int uid) const;

        /// Get contiguous camera index (for controller pool integration)
        int camera_index(int camera_id) const;

        /// Forward pass: apply ISP pipeline (exposure, vignetting, color correction, CRF)
        /// @param camera_id Original COLMAP camera_id (translated internally)
        /// @param uid Original frame UID (translated internally)
        lfs::core::Tensor apply(const lfs::core::Tensor& rgb, int camera_id, int uid);

        /// Apply ISP with controller-predicted params (for novel view synthesis)
        /// @param rgb input image [C,H,W]
        /// @param controller_params [1,9] = [exposure, color_params[0:8]]
        /// @param camera_idx camera index for vignetting/CRF (use 0 or averaged if unknown)
        lfs::core::Tensor apply_with_controller_params(const lfs::core::Tensor& rgb,
                                                       const lfs::core::Tensor& controller_params,
                                                       int camera_idx = 0);

        /// Apply ISP with controller-predicted params + user overrides (for novel view with adjustments)
        lfs::core::Tensor apply_with_controller_params_and_overrides(const lfs::core::Tensor& rgb,
                                                                     const lfs::core::Tensor& controller_params,
                                                                     int camera_idx,
                                                                     const PPISPRenderOverrides& overrides);

        /// Apply ISP with user-controlled overrides (for viewport preview)
        /// Full control over all PPISP parameters as described in paper Section 4 and Supplementary D
        /// @param camera_id Original COLMAP camera_id (translated internally)
        /// @param uid Original frame UID (translated internally)
        lfs::core::Tensor apply_with_overrides(const lfs::core::Tensor& rgb, int camera_id, int uid,
                                               const PPISPRenderOverrides& overrides);

        /// Backward pass: accumulate gradients (call optimizer_step after all backward calls)
        /// @param camera_id Original COLMAP camera_id (translated internally)
        /// @param uid Original frame UID (translated internally)
        lfs::core::Tensor backward(const lfs::core::Tensor& rgb, const lfs::core::Tensor& grad_output, int camera_id,
                                   int uid);

        /// Compute regularization loss (returns GPU tensor for async accumulation)
        lfs::core::Tensor reg_loss_gpu();

        /// Accumulate regularization gradients
        void reg_backward();

        /// Apply Adam with all accumulated gradients
        void optimizer_step();

        /// Clear gradients for next iteration
        void zero_grad();

        /// Update learning rate schedule
        void scheduler_step();

        // Accessors
        int num_cameras() const { return num_cameras_; }
        int num_frames() const { return num_frames_; }
        double get_lr() const { return current_lr_; }
        int64_t get_step() const { return step_; }
        const Config& get_config() const { return config_; }

        /// Get learned parameters for a specific frame as [1,9] tensor
        /// Returns: [exposure, color_params[0:8]] for distillation target
        /// @param uid Original frame UID (translated internally)
        lfs::core::Tensor get_params_for_frame(int uid) const;

        // Serialization (full state for checkpoints)
        void serialize(std::ostream& os) const;
        void deserialize(std::istream& is);

        // Inference-only serialization (weights only, no Adam state)
        void serialize_inference(std::ostream& os) const;
        void deserialize_inference(std::istream& is);

    private:
        void compute_bias_corrections(float& bc1_rcp, float& bc2_sqrt_rcp) const {
            const double bc1 = 1.0 - std::pow(config_.beta1, step_ + 1);
            const double bc2 = 1.0 - std::pow(config_.beta2, step_ + 1);
            bc1_rcp = static_cast<float>(1.0 / bc1);
            bc2_sqrt_rcp = static_cast<float>(1.0 / std::sqrt(bc2));
        }

        int translate_camera(int camera_id) const;
        int translate_frame(int uid) const;

        void allocate_tensors();

        // Parameter tensors (all on GPU)
        // Exposure: [num_frames] - per-frame exposure in log-space
        lfs::core::Tensor exposure_params_;
        lfs::core::Tensor exposure_exp_avg_;
        lfs::core::Tensor exposure_exp_avg_sq_;
        lfs::core::Tensor exposure_grad_;

        // Vignetting: [num_cameras, 3, 5] - per-camera per-channel vignetting
        // 5 params: cx, cy, alpha0, alpha1, alpha2
        lfs::core::Tensor vignetting_params_;
        lfs::core::Tensor vignetting_exp_avg_;
        lfs::core::Tensor vignetting_exp_avg_sq_;
        lfs::core::Tensor vignetting_grad_;

        // Color: [num_frames, 8] - per-frame color latent offsets
        // 8 params: 4 color points x 2D offsets
        lfs::core::Tensor color_params_;
        lfs::core::Tensor color_exp_avg_;
        lfs::core::Tensor color_exp_avg_sq_;
        lfs::core::Tensor color_grad_;

        // CRF: [num_cameras, 3, 4] - per-camera per-channel CRF
        // 4 params: toe, shoulder, gamma, center
        lfs::core::Tensor crf_params_;
        lfs::core::Tensor crf_exp_avg_;
        lfs::core::Tensor crf_exp_avg_sq_;
        lfs::core::Tensor crf_grad_;

        Config config_;
        int64_t step_ = 0;
        double current_lr_;
        double initial_lr_;
        int total_iterations_;
        int num_cameras_ = 0;
        int num_frames_ = 0;

        // ID translation maps
        std::unordered_map<int, int> camera_id_to_idx_;
        std::unordered_map<int, int> uid_to_frame_idx_;
        std::unordered_map<int, int> uid_to_camera_id_;
        bool finalized_ = false;

        // ZCA pinv block-diagonal matrix for color mean regularization [8x8]
        lfs::core::Tensor color_pinv_block_diag_;
    };

} // namespace lfs::training
