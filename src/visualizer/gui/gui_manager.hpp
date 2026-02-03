/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/events.hpp"
#include "core/parameters.hpp"
#include "core/path_utils.hpp"
#include "gui/gizmo_transform.hpp"
#include "gui/panels/menu_bar.hpp"
#include "gui/ui_context.hpp"
#include "gui/utils/drag_drop_native.hpp"
#include "io/loader.hpp"
#include "io/video/video_export_options.hpp"
#include "sequencer/sequencer_controller.hpp"
#include "sequencer/sequencer_panel.hpp"
#include "windows/disk_space_error_dialog.hpp"
#include "windows/video_extractor_dialog.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <imgui.h>
#include <ImGuizmo.h>

namespace lfs::core {
    class SplatData;
}

namespace lfs::vis {
    class VisualizerImpl;

    namespace gui {
        class FileBrowser;
        class ProjectChangedDialogBox;

        namespace panels {
            struct SequencerUIState {
                bool show_camera_path = true;
                bool snap_to_grid = false;
                float snap_interval = 0.5f;
                float playback_speed = 1.0f;
                bool follow_playback = false;
                float pip_preview_scale = 1.0f;
                lfs::io::video::VideoPreset preset = lfs::io::video::VideoPreset::YOUTUBE_1080P;
                int custom_width = 1920;
                int custom_height = 1080;
                int framerate = 30;
                int quality = 18;
            };
        } // namespace panels

        class GuiManager {
        public:
            GuiManager(VisualizerImpl* viewer);
            ~GuiManager();

            // Lifecycle
            void init();
            void shutdown();
            void render();

            // State queries
            bool wantsInput() const;
            bool isAnyWindowActive() const;
            bool needsAnimationFrame() const;

            // Window visibility
            void showWindow(const std::string& name, bool show = true);
            void toggleWindow(const std::string& name);

            void setFileSelectedCallback(std::function<void(const std::filesystem::path&, bool)> callback);

            // Viewport region access
            ImVec2 getViewportPos() const;
            ImVec2 getViewportSize() const;
            bool isMouseInViewport() const;
            bool isViewportFocused() const;
            bool isPositionInViewport(double x, double y) const;
            bool isViewportGizmoDragging() const { return viewport_gizmo_dragging_; }
            bool isResizingPanel() const {
                return resizing_panel_ || hovering_panel_edge_ ||
                       python_console_resizing_ || python_console_hovering_edge_;
            }
            bool isPositionInViewportGizmo(double x, double y) const;

            // Selection sub-mode shortcuts (Ctrl+1..5)
            void setSelectionSubMode(SelectionSubMode mode);
            [[nodiscard]] SelectionSubMode getSelectionSubMode() const { return selection_mode_; }
            [[nodiscard]] ToolType getCurrentToolMode() const;

            // Transform gizmo settings
            [[nodiscard]] TransformSpace getTransformSpace() const { return transform_space_; }
            void setTransformSpace(TransformSpace space) { transform_space_ = space; }
            [[nodiscard]] PivotMode getPivotMode() const { return pivot_mode_; }
            void setPivotMode(PivotMode mode) { pivot_mode_ = mode; }
            [[nodiscard]] ImGuizmo::OPERATION getCurrentOperation() const { return current_operation_; }
            void setCurrentOperation(ImGuizmo::OPERATION op) { current_operation_ = op; }

            // Gizmo manipulation state (for wireframe sync)
            bool isCropboxGizmoActive() const { return cropbox_gizmo_active_; }
            bool isEllipsoidGizmoActive() const { return ellipsoid_gizmo_active_; }

            bool isForceExit() const { return force_exit_; }
            void setForceExit(bool value) { force_exit_ = value; }

            [[nodiscard]] SequencerController& sequencer() { return sequencer_controller_; }
            [[nodiscard]] const SequencerController& sequencer() const { return sequencer_controller_; }

            [[nodiscard]] bool isSequencerVisible() const { return show_sequencer_; }
            void setSequencerVisible(bool visible) { show_sequencer_ = visible; }

            [[nodiscard]] panels::SequencerUIState& getSequencerUIState() { return sequencer_ui_state_; }
            [[nodiscard]] const panels::SequencerUIState& getSequencerUIState() const { return sequencer_ui_state_; }

            [[nodiscard]] VisualizerImpl* getViewer() const { return viewer_; }
            [[nodiscard]] std::unordered_map<std::string, bool>* getWindowStates() { return &window_states_; }

            void requestExitConfirmation();
            bool isExitConfirmationPending() const;

            void performExport(lfs::core::ExportFormat format, const std::filesystem::path& path,
                               const std::vector<std::string>& node_names, int sh_degree);

            bool isCapturingInput() const;
            bool isModalWindowOpen() const;
            [[nodiscard]] bool isStartupVisible() const { return show_startup_overlay_; }
            void captureKey(int key, int mods);
            void captureMouseButton(int button, int mods);

            // Thumbnail system (delegates to MenuBar)
            void requestThumbnail(const std::string& video_id);
            void processThumbnails();
            bool isThumbnailReady(const std::string& video_id) const;
            uint64_t getThumbnailTexture(const std::string& video_id) const;

            int getHighlightedCameraUid() const;

            // Drag-drop state for overlays
            [[nodiscard]] bool isDragHovering() const { return drag_drop_hovering_; }

            // Export state accessors for Python overlays
            [[nodiscard]] float getExportProgress() const { return export_state_.progress.load(); }
            [[nodiscard]] std::string getExportStage() const {
                std::lock_guard lock(export_state_.mutex);
                return export_state_.stage;
            }
            [[nodiscard]] lfs::core::ExportFormat getExportFormat() const {
                std::lock_guard lock(export_state_.mutex);
                return export_state_.format;
            }

            // Export control
            [[nodiscard]] bool isExporting() const { return export_state_.active.load(); }
            void cancelExport();

            // Import state accessors for Python overlays
            [[nodiscard]] bool isImporting() const { return import_state_.active.load(); }
            [[nodiscard]] bool isImportCompletionShowing() const { return import_state_.show_completion.load(); }
            [[nodiscard]] float getImportProgress() const { return import_state_.progress.load(); }
            [[nodiscard]] std::string getImportStage() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.stage;
            }
            [[nodiscard]] std::string getImportDatasetType() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.dataset_type;
            }
            [[nodiscard]] std::string getImportPath() const {
                std::lock_guard lock(import_state_.mutex);
                return lfs::core::path_to_utf8(import_state_.path.filename());
            }
            [[nodiscard]] bool getImportSuccess() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.success;
            }
            [[nodiscard]] std::string getImportError() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.error;
            }
            [[nodiscard]] size_t getImportNumImages() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.num_images;
            }
            [[nodiscard]] size_t getImportNumPoints() const {
                std::lock_guard lock(import_state_.mutex);
                return import_state_.num_points;
            }
            [[nodiscard]] float getImportSecondsSinceCompletion() const {
                if (!import_state_.show_completion.load())
                    return 0.0f;
                std::lock_guard lock(import_state_.mutex);
                auto elapsed = std::chrono::steady_clock::now() - import_state_.completion_time;
                return std::chrono::duration<float>(elapsed).count();
            }
            void dismissImport() { import_state_.show_completion.store(false); }

            // Video export state accessors for Python overlays
            [[nodiscard]] bool isExportingVideo() const { return video_export_state_.active.load(); }
            [[nodiscard]] float getVideoExportProgress() const { return video_export_state_.progress.load(); }
            [[nodiscard]] int getVideoExportCurrentFrame() const { return video_export_state_.current_frame.load(); }
            [[nodiscard]] int getVideoExportTotalFrames() const { return video_export_state_.total_frames.load(); }
            [[nodiscard]] std::string getVideoExportStage() const {
                std::lock_guard lock(video_export_state_.mutex);
                return video_export_state_.stage;
            }
            void cancelVideoExport();

        private:
            void setupEventHandlers();
            void checkCudaVersionAndNotify();
            void applyDefaultStyle();
            void updateViewportRegion();
            void updateViewportFocus();
            void initMenuBar();

            // Core dependencies
            VisualizerImpl* viewer_;

            // Owned components
            std::unique_ptr<FileBrowser> file_browser_;
            std::unique_ptr<DiskSpaceErrorDialog> disk_space_error_dialog_;
            std::unique_ptr<lfs::gui::VideoExtractorDialog> video_extractor_dialog_;

            // UI state only
            std::unordered_map<std::string, bool> window_states_;
            bool show_main_panel_ = true;
            bool show_viewport_gizmo_ = true;

            // Viewport region tracking
            ImVec2 viewport_pos_;
            ImVec2 viewport_size_;
            bool viewport_has_focus_;
            bool force_exit_ = false;

            // Right panel state
            float right_panel_width_ = 300.0f;
            bool resizing_panel_ = false;
            bool hovering_panel_edge_ = false;
            static constexpr float RIGHT_PANEL_MIN_RATIO = 0.01f;
            static constexpr float RIGHT_PANEL_MAX_RATIO = 0.99f;
            float scene_panel_ratio_ = 0.4f; // Scene panel vs tabs vertical split

            // Python console panel state (docked mode)
            float python_console_width_ = -1.0f; // -1 = uninitialized, will be set to 1:1 split on first open
            bool python_console_resizing_ = false;
            bool python_console_hovering_edge_ = false;
            static constexpr float PYTHON_CONSOLE_MIN_WIDTH = 200.0f;
            static constexpr float PYTHON_CONSOLE_MAX_RATIO = 0.5f;

            // Viewport gizmo layout (must match ViewportGizmo settings)
            static constexpr float VIEWPORT_GIZMO_SIZE = 95.0f;
            static constexpr float VIEWPORT_GIZMO_MARGIN_X = 10.0f;
            static constexpr float VIEWPORT_GIZMO_MARGIN_Y = 10.0f;

            // Method declarations
            void renderSequencerPanel(const UIContext& ctx);
            void renderCameraPath(const UIContext& ctx);
            void renderDockedPythonConsole(const UIContext& ctx, float panel_x, float panel_h);
            void renderPythonPanels(const UIContext& ctx);
            void renderCropBoxGizmo(const UIContext& ctx);
            void renderEllipsoidGizmo(const UIContext& ctx);
            void renderCropGizmoMiniToolbar(const UIContext& ctx);
            void renderNodeTransformGizmo(const UIContext& ctx);

            std::unique_ptr<MenuBar> menu_bar_;

            // Camera sequencer
            SequencerController sequencer_controller_;
            std::unique_ptr<SequencerPanel> sequencer_panel_;
            bool keyframe_context_menu_open_ = false;
            std::optional<size_t> context_menu_keyframe_;

            // Keyframe gizmo
            ImGuizmo::OPERATION keyframe_gizmo_op_ = ImGuizmo::OPERATION(0);
            bool keyframe_gizmo_active_ = false;
            glm::vec3 keyframe_pos_before_drag_{0.0f};
            glm::quat keyframe_rot_before_drag_{1.0f, 0.0f, 0.0f, 0.0f};
            void renderKeyframeGizmo(const UIContext& ctx);

            // Keyframe preview (PiP)
            static constexpr int PREVIEW_WIDTH = 320;
            static constexpr int PREVIEW_HEIGHT = 180;
            static constexpr float PREVIEW_TARGET_FPS = 30.0f;
            unsigned int pip_fbo_ = 0;
            unsigned int pip_texture_ = 0;
            unsigned int pip_depth_rbo_ = 0;
            bool pip_initialized_ = false;
            std::optional<size_t> pip_last_keyframe_;
            bool pip_needs_update_ = true;
            std::chrono::steady_clock::time_point pip_last_render_time_;
            void initPipPreview();
            void renderKeyframePreview(const UIContext& ctx);
            void drawPipPreviewWindow(const UIContext& ctx);

            // Node transform gizmo state
            bool show_node_gizmo_ = true;
            ImGuizmo::OPERATION node_gizmo_operation_ = ImGuizmo::TRANSLATE;

            // Transform gizmo state (replaces GizmoToolbarState)
            ImGuizmo::OPERATION current_operation_ = ImGuizmo::TRANSLATE;
            SelectionSubMode selection_mode_ = SelectionSubMode::Centers;
            TransformSpace transform_space_ = TransformSpace::Local;
            PivotMode pivot_mode_ = PivotMode::Origin;
            bool show_sequencer_ = false;

            // Unified gizmo context for cropbox/ellipsoid
            GizmoTransformContext gizmo_context_;

            // Cropbox gizmo state
            bool cropbox_gizmo_active_ = false;
            std::string cropbox_node_name_;

            // Ellipsoid gizmo state
            bool ellipsoid_gizmo_active_ = false;
            std::string ellipsoid_node_name_;

            // Node transform undo/redo state (supports multi-selection)
            bool node_gizmo_active_ = false;
            std::vector<std::string> node_gizmo_node_names_;
            std::vector<glm::mat4> node_transforms_before_drag_;
            std::vector<glm::vec3> node_original_world_positions_;
            std::vector<glm::mat4> node_parent_world_inverses_;
            std::vector<glm::mat3> node_original_rotations_;
            std::vector<glm::vec3> node_original_scales_;
            glm::vec3 gizmo_pivot_{0.0f};
            glm::mat3 gizmo_cumulative_rotation_{1.0f};
            glm::vec3 gizmo_cumulative_scale_{1.0f};

            // Previous tool/selection mode for detecting changes
            std::string previous_tool_id_;
            SelectionSubMode previous_selection_mode_ = SelectionSubMode::Centers;

            // Tool cleanup
            void deactivateAllTools();

            // Crop box flash effect
            std::chrono::steady_clock::time_point crop_flash_start_;
            bool crop_flash_active_ = false;
            void triggerCropFlash();
            void updateCropFlash();

            std::string focus_panel_name_;
            bool ui_hidden_ = false;

            // Font storage
            ImFont* font_regular_ = nullptr;
            ImFont* font_bold_ = nullptr;
            ImFont* font_heading_ = nullptr;
            ImFont* font_small_ = nullptr;
            ImFont* font_section_ = nullptr;
            ImFont* font_monospace_ = nullptr;
            ImFont* mono_fonts_[FontSet::MONO_SIZE_COUNT] = {};
            float mono_font_scales_[FontSet::MONO_SIZE_COUNT] = {};
            FontSet buildFontSet() const;

            // Viewport gizmo drag-to-orbit state
            bool viewport_gizmo_dragging_ = false;
            glm::dvec2 gizmo_drag_start_cursor_{0.0, 0.0};

            // Async export state
            struct ExportState {
                std::atomic<bool> active{false};
                std::atomic<bool> cancel_requested{false};
                std::atomic<float> progress{0.0f};
                lfs::core::ExportFormat format{lfs::core::ExportFormat::PLY}; // Protected by mutex
                std::string stage;                                            // Protected by mutex
                std::string error;                                            // Protected by mutex
                mutable std::mutex mutex;
                std::unique_ptr<std::jthread> thread;
            };
            ExportState export_state_;

            // Video export state
            struct VideoExportState {
                std::atomic<bool> active{false};
                std::atomic<bool> cancel_requested{false};
                std::atomic<float> progress{0.0f};
                std::atomic<int> current_frame{0};
                std::atomic<int> total_frames{0};
                std::string stage;
                std::string error;
                mutable std::mutex mutex;
                std::unique_ptr<std::jthread> thread;
            };
            VideoExportState video_export_state_;

            // Async dataset import state
            struct ImportState {
                std::atomic<bool> active{false};
                std::atomic<bool> show_completion{false};
                std::atomic<bool> load_complete{false};
                std::atomic<float> progress{0.0f};
                mutable std::mutex mutex;
                // Protected by mutex:
                std::filesystem::path path;
                std::string stage;
                std::string dataset_type;
                std::string error;
                size_t num_images{0};
                size_t num_points{0};
                bool success{false};
                std::chrono::steady_clock::time_point completion_time;
                std::optional<lfs::io::LoadResult> load_result;
                lfs::core::param::TrainingParameters params;
                std::unique_ptr<std::jthread> thread;
            };
            ImportState import_state_;

            // Sequencer UI state
            panels::SequencerUIState sequencer_ui_state_;

            void startAsyncImport(const std::filesystem::path& path,
                                  const lfs::core::param::TrainingParameters& params);
            void checkAsyncImportCompletion();
            void applyLoadedDataToScene();

            void renderStartupOverlay();

            // Startup overlay state
            bool show_startup_overlay_ = true;
            unsigned int startup_logo_light_texture_ = 0;
            unsigned int startup_logo_dark_texture_ = 0;
            unsigned int startup_core11_light_texture_ = 0;
            unsigned int startup_core11_dark_texture_ = 0;
            int startup_logo_width_ = 0, startup_logo_height_ = 0;
            int startup_core11_width_ = 0, startup_core11_height_ = 0;
            void startAsyncExport(lfs::core::ExportFormat format,
                                  const std::filesystem::path& path,
                                  std::unique_ptr<lfs::core::SplatData> data);
            void startVideoExport(const std::filesystem::path& path,
                                  const io::video::VideoExportOptions& options);

            // Native drag-drop handler
            NativeDragDrop drag_drop_;
            bool drag_drop_hovering_ = false;
        };
    } // namespace gui
} // namespace lfs::vis
