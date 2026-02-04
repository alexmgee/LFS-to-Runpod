/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

// glad must be included before OpenGL headers
// clang-format off
#include <glad/glad.h>
// clang-format on

#include "gui/gui_manager.hpp"
#include "core/cuda_version.hpp"
#include "core/event_bridge/localization_manager.hpp"
#include "core/image_io.hpp"
#include "core/logger.hpp"
#include "core/path_utils.hpp"
#include "gui/editor/python_editor.hpp"
#include "gui/html_viewer_export.hpp"
#include "gui/panels/python_console_panel.hpp"
#include "gui/panels/tools_panel.hpp"
#include "gui/panels/windows_console_utils.hpp"
#include "gui/string_keys.hpp"
#include "gui/ui_widgets.hpp"
#include "gui/utils/windows_utils.hpp"
#include "gui/windows/file_browser.hpp"
#include "io/exporter.hpp"
#include "io/loader.hpp"
#include "io/video/video_encoder.hpp"
#include "io/video_frame_extractor.hpp"
#include "sequencer/keyframe.hpp"
#include <implot.h>

#include "input/input_controller.hpp"
#include "internal/resource_paths.hpp"
#include "tools/align_tool.hpp"

#include "core/data_loading_service.hpp"
#include "core/events.hpp"
#include "core/parameters.hpp"
#include "core/services.hpp"
#include "operator/operator_id.hpp"
#include "operator/operator_registry.hpp"
#include "python/package_manager.hpp"
#include "python/python_runtime.hpp"
#include "python/ui_hooks.hpp"
#include "rendering/rendering.hpp"
#include "rendering/rendering_manager.hpp"
#include "scene/scene.hpp"
#include "scene/scene_manager.hpp"
#include "theme/theme.hpp"
#include "tools/brush_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/unified_tool_registry.hpp"
#include "training/training_state.hpp"
#include "visualizer_impl.hpp"
#include <cuda_runtime.h>

#include "git_version.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <format>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <ImGuizmo.h>

namespace lfs::vis::gui {

    // Import commonly used types
    using ToolType = lfs::vis::ToolType;
    using ExportFormat = lfs::core::ExportFormat;

    // Layout constants
    constexpr float STATUS_BAR_HEIGHT = 22.0f;

    constexpr float GIZMO_AXIS_LIMIT = 0.0001f;
    constexpr float MIN_GIZMO_SCALE = 0.001f;

    namespace {
        inline glm::mat3 extractRotation(const glm::mat4& m) {
            return glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                             glm::normalize(glm::vec3(m[2])));
        }

        inline glm::vec3 extractScale(const glm::mat4& m) {
            return glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                             glm::length(glm::vec3(m[2])));
        }
    } // namespace

    // Returns display name for dataset type
    [[nodiscard]] const char* getDatasetTypeName(const std::filesystem::path& path) {
        switch (lfs::io::Loader::getDatasetType(path)) {
        case lfs::io::DatasetType::COLMAP: return "COLMAP";
        case lfs::io::DatasetType::Transforms: return "NeRF/Blender";
        default: return "Dataset";
        }
    }

    // Truncate SH to target degree. shN has (d+1)²-1 coefficients for degree d.
    void truncateSHDegree(lfs::core::SplatData& splat, const int target_degree) {
        if (target_degree >= splat.get_max_sh_degree())
            return;

        if (target_degree == 0) {
            splat.shN() = lfs::core::Tensor{};
        } else {
            const size_t keep_coeffs = static_cast<size_t>((target_degree + 1) * (target_degree + 1) - 1);
            auto& shN = splat.shN();
            if (shN.is_valid() && shN.ndim() >= 2 && shN.shape()[1] > keep_coeffs) {
                if (shN.ndim() == 3) {
                    shN = shN.slice(1, 0, static_cast<int64_t>(keep_coeffs)).contiguous();
                } else {
                    constexpr size_t CHANNELS = 3;
                    shN = shN.slice(1, 0, static_cast<int64_t>(keep_coeffs * CHANNELS)).contiguous();
                }
            }
        }
        splat.set_max_sh_degree(target_degree);
        splat.set_active_sh_degree(target_degree);
    }

    GuiManager::GuiManager(VisualizerImpl* viewer)
        : viewer_(viewer) {

        // Create components
        file_browser_ = std::make_unique<FileBrowser>();
        menu_bar_ = std::make_unique<MenuBar>();
        sequencer_panel_ = std::make_unique<SequencerPanel>(sequencer_controller_);
        disk_space_error_dialog_ = std::make_unique<DiskSpaceErrorDialog>();
        video_extractor_dialog_ = std::make_unique<lfs::gui::VideoExtractorDialog>();

        // Wire up video extractor dialog callback
        video_extractor_dialog_->setOnStartExtraction([this](const lfs::gui::VideoExtractionParams& params) {
            auto* dialog = video_extractor_dialog_.get();
            std::thread([params, dialog]() {
                lfs::io::VideoFrameExtractor extractor;

                lfs::io::VideoFrameExtractor::Params extract_params;
                extract_params.video_path = params.video_path;
                extract_params.output_dir = params.output_dir;
                extract_params.mode = params.mode;
                extract_params.fps = params.fps;
                extract_params.frame_interval = params.frame_interval;
                extract_params.format = params.format;
                extract_params.jpg_quality = params.jpg_quality;
                extract_params.start_time = params.start_time;
                extract_params.end_time = params.end_time;
                extract_params.resolution_mode = params.resolution_mode;
                extract_params.scale = params.scale;
                extract_params.custom_width = params.custom_width;
                extract_params.custom_height = params.custom_height;
                extract_params.filename_pattern = params.filename_pattern;

                extract_params.progress_callback = [dialog](int current, int total) {
                    dialog->updateProgress(current, total);
                };

                std::string error;
                if (!extractor.extract(extract_params, error)) {
                    LOG_ERROR("Video frame extraction failed: {}", error);
                    dialog->setExtractionError(error);
                } else {
                    LOG_INFO("Video frame extraction completed successfully");
                    dialog->setExtractionComplete();
                }
            }).detach();
        });

        // Initialize window states
        window_states_["file_browser"] = false;
        window_states_["scene_panel"] = true;
        window_states_["system_console"] = false;
        window_states_["training_tab"] = false;
        window_states_["export_dialog"] = false;
        window_states_["python_console"] = false;
        window_states_["video_extractor_dialog"] = false;

        // Initialize focus state
        viewport_has_focus_ = false;

        setupEventHandlers();
        checkCudaVersionAndNotify();
    }

    void GuiManager::checkCudaVersionAndNotify() {
        using namespace lfs::core;
        const auto info = check_cuda_version();
        if (!info.query_failed && !info.supported) {
            constexpr int MIN_MAJOR = MIN_CUDA_VERSION / 1000;
            constexpr int MIN_MINOR = (MIN_CUDA_VERSION % 1000) / 10;
            events::state::CudaVersionUnsupported{
                .major = info.major,
                .minor = info.minor,
                .min_major = MIN_MAJOR,
                .min_minor = MIN_MINOR}
                .emit();
        }
    }

    GuiManager::~GuiManager() {
        // Cancel and wait for export thread if running
        if (export_state_.active.load()) {
            cancelExport();
            if (export_state_.thread && export_state_.thread->joinable()) {
                export_state_.thread->join();
            }
        }
        export_state_.thread.reset();
    }

    void GuiManager::initMenuBar() {
        menu_bar_->setOnShowPythonConsole([this]() {
            window_states_["python_console"] = !window_states_["python_console"];
        });
    }

    FontSet GuiManager::buildFontSet() const {
        FontSet fs{font_regular_, font_bold_, font_heading_, font_small_, font_section_, font_monospace_};
        for (int i = 0; i < FontSet::MONO_SIZE_COUNT; ++i) {
            fs.monospace_sized[i] = mono_fonts_[i];
            fs.monospace_sizes[i] = mono_font_scales_[i];
        }
        return fs;
    }

    void GuiManager::init() {
        // ImGui initialization
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        // Share ImGui state with Python module across DLL boundaries
        ImGuiContext* const ctx = ImGui::GetCurrentContext();
        lfs::python::set_imgui_context(ctx);

        ImGuiMemAllocFunc alloc_fn{};
        ImGuiMemFreeFunc free_fn{};
        void* alloc_user_data{};
        ImGui::GetAllocatorFunctions(&alloc_fn, &free_fn, &alloc_user_data);
        lfs::python::set_imgui_allocator_functions(
            reinterpret_cast<void*>(alloc_fn),
            reinterpret_cast<void*>(free_fn),
            alloc_user_data);
        lfs::python::set_implot_context(ImPlot::GetCurrentContext());

        lfs::python::set_gl_texture_service(
            [](const unsigned char* data, const int w, const int h, const int channels) -> lfs::python::TextureResult {
                if (!data || w <= 0 || h <= 0)
                    return {0, 0, 0};

                GLuint tex = 0;
                glGenTextures(1, &tex);
                if (tex == 0)
                    return {0, 0, 0};

                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                GLenum format = GL_RGB;
                GLenum internal_format = GL_RGB8;
                if (channels == 1) {
                    format = GL_RED;
                    internal_format = GL_R8;
                } else if (channels == 4) {
                    format = GL_RGBA;
                    internal_format = GL_RGBA8;
                }

                glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, GL_UNSIGNED_BYTE, data);

                if (channels == 1) {
                    GLint swizzle[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
                }

                glBindTexture(GL_TEXTURE_2D, 0);
                return {tex, w, h};
            },
            [](const uint32_t tex) {
                if (tex > 0) {
                    const auto gl_tex = static_cast<GLuint>(tex);
                    glDeleteTextures(1, &gl_tex);
                }
            },
            []() -> int {
                constexpr int FALLBACK_MAX_TEXTURE_SIZE = 4096;
                GLint sz = 0;
                glGetIntegerv(GL_MAX_TEXTURE_SIZE, &sz);
                return sz > 0 ? sz : FALLBACK_MAX_TEXTURE_SIZE;
            });

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        // Platform/Renderer initialization
        ImGui_ImplGlfw_InitForOpenGL(viewer_->getWindow(), true);
        ImGui_ImplOpenGL3_Init("#version 430");

        // Initialize localization system
        auto& loc = lfs::event::LocalizationManager::getInstance();
        const std::string locale_path = lfs::core::path_to_utf8(lfs::core::getLocalesDir());
        if (!loc.initialize(locale_path)) {
            LOG_WARN("Failed to initialize localization system, using default strings");
        } else {
            LOG_INFO("Localization initialized with language: {}", loc.getCurrentLanguageName());
        }

        float xscale, yscale;
        glfwGetWindowContentScale(viewer_->getWindow(), &xscale, &yscale);

        // Clamping / safety net for weird DPI values
        // Support up to 4.0x scale for high-DPI displays (e.g., 6K monitors)
        xscale = std::clamp(xscale, 1.0f, 4.0f);

        // Store DPI scale for use by UI components
        lfs::python::set_shared_dpi_scale(xscale);
        lfs::vis::setThemeDpiScale(xscale);

        // Set application icon - use the resource path helper
        try {
            const auto icon_path = lfs::vis::getAssetPath("lichtfeld-icon.png");
            const auto [data, width, height, channels] = lfs::core::load_image_with_alpha(icon_path);

            GLFWimage image{width, height, data};
            glfwSetWindowIcon(viewer_->getWindow(), 1, &image);
            lfs::core::free_image(data);
        } catch (const std::exception& e) {
            LOG_WARN("Could not load application icon: {}", e.what());
        }

        // Apply theme first to get font settings
        applyDefaultStyle();

        // Load fonts
        const auto& t = theme();
        try {
            const auto regular_path = lfs::vis::getAssetPath("fonts/" + t.fonts.regular_path);
            const auto bold_path = lfs::vis::getAssetPath("fonts/" + t.fonts.bold_path);
            const auto japanese_path = lfs::vis::getAssetPath("fonts/NotoSansJP-Regular.ttf");
            const auto korean_path = lfs::vis::getAssetPath("fonts/NotoSansKR-Regular.ttf");

            // Helper to check if font file is valid
            const auto is_font_valid = [](const std::filesystem::path& path) -> bool {
                constexpr size_t MIN_FONT_FILE_SIZE = 100;
                return std::filesystem::exists(path) && std::filesystem::file_size(path) >= MIN_FONT_FILE_SIZE;
            };

            // Load font with optional CJK glyph merging (Japanese + Korean)
            const auto load_font_with_cjk =
                [&](const std::filesystem::path& path, const float size) -> ImFont* {
                if (!is_font_valid(path)) {
                    LOG_WARN("Font file invalid: {}", lfs::core::path_to_utf8(path));
                    return nullptr;
                }

                // Load base font (Latin characters)
                const std::string path_utf8 = lfs::core::path_to_utf8(path);
                ImFont* font = io.Fonts->AddFontFromFileTTF(path_utf8.c_str(), size);
                if (!font)
                    return nullptr;

                // Merge Japanese + Chinese glyphs if available (NotoSansJP contains both)
                if (is_font_valid(japanese_path)) {
                    ImFontConfig config;
                    config.MergeMode = true;
                    const std::string japanese_path_utf8 = lfs::core::path_to_utf8(japanese_path);
                    io.Fonts->AddFontFromFileTTF(japanese_path_utf8.c_str(), size, &config,
                                                 io.Fonts->GetGlyphRangesJapanese());
                    // Chinese glyphs are also in NotoSansJP, just need to load the ranges
                    io.Fonts->AddFontFromFileTTF(japanese_path_utf8.c_str(), size, &config,
                                                 io.Fonts->GetGlyphRangesChineseFull());
                }

                // Merge Korean glyphs if available
                if (is_font_valid(korean_path)) {
                    ImFontConfig config;
                    config.MergeMode = true;
                    const std::string korean_path_utf8 = lfs::core::path_to_utf8(korean_path);
                    io.Fonts->AddFontFromFileTTF(korean_path_utf8.c_str(), size, &config,
                                                 io.Fonts->GetGlyphRangesKorean());
                }

                return font;
            };

            font_regular_ = load_font_with_cjk(regular_path, t.fonts.base_size * xscale);
            font_bold_ = load_font_with_cjk(bold_path, t.fonts.base_size * xscale);
            font_heading_ = load_font_with_cjk(bold_path, t.fonts.heading_size * xscale);
            font_small_ = load_font_with_cjk(regular_path, t.fonts.small_size * xscale);
            font_section_ = load_font_with_cjk(bold_path, t.fonts.section_size * xscale);

            // Monospace font at multiple sizes for crisp scaling
            const auto monospace_path = lfs::vis::getAssetPath("fonts/JetBrainsMono-Regular.ttf");
            if (is_font_valid(monospace_path)) {
                const std::string mono_path_utf8 = lfs::core::path_to_utf8(monospace_path);

                static constexpr ImWchar GLYPH_RANGES[] = {
                    0x0020,
                    0x00FF, // Basic Latin + Latin Supplement
                    0x2190,
                    0x21FF, // Arrows
                    0x2500,
                    0x257F, // Box Drawing
                    0x2580,
                    0x259F, // Block Elements
                    0x25A0,
                    0x25FF, // Geometric Shapes
                    0,
                };

                static constexpr float MONO_SCALES[] = {0.7f, 1.0f, 1.3f, 1.7f, 2.2f};
                static_assert(std::size(MONO_SCALES) == FontSet::MONO_SIZE_COUNT);

                for (int i = 0; i < FontSet::MONO_SIZE_COUNT; ++i) {
                    ImFontConfig config;
                    config.GlyphRanges = GLYPH_RANGES;
                    const float size = t.fonts.base_size * xscale * MONO_SCALES[i];
                    mono_fonts_[i] = io.Fonts->AddFontFromFileTTF(mono_path_utf8.c_str(), size, &config);
                    mono_font_scales_[i] = MONO_SCALES[i];
                }
                font_monospace_ = mono_fonts_[1];
                if (font_monospace_) {
                    LOG_INFO("Loaded monospace font: JetBrainsMono-Regular.ttf ({} sizes)", FontSet::MONO_SIZE_COUNT);
                }
            }
            if (!font_monospace_) {
                font_monospace_ = font_regular_;
                LOG_WARN("Monospace font not found, using regular font for code editor");
            }

            const bool all_loaded = font_regular_ && font_bold_ && font_heading_ && font_small_ && font_section_;
            if (!all_loaded) {
                LOG_WARN("Some fonts failed to load, using fallback");
                ImFont* const fallback = font_regular_ ? font_regular_ : io.Fonts->AddFontDefault();
                if (!font_regular_)
                    font_regular_ = fallback;
                if (!font_bold_)
                    font_bold_ = fallback;
                if (!font_heading_)
                    font_heading_ = fallback;
                if (!font_small_)
                    font_small_ = fallback;
                if (!font_section_)
                    font_section_ = fallback;
            } else {
                LOG_INFO("Loaded fonts: {} and {}", t.fonts.regular_path, t.fonts.bold_path);
                if (is_font_valid(japanese_path)) {
                    LOG_INFO("Japanese + Chinese font support enabled");
                }
                if (is_font_valid(korean_path)) {
                    LOG_INFO("Korean font support enabled");
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Font loading failed: {}", e.what());
            ImFont* const fallback = io.Fonts->AddFontDefault();
            font_regular_ = font_bold_ = font_heading_ = font_small_ = font_section_ = fallback;
        }

        setFileSelectedCallback([this](const std::filesystem::path& path, const bool is_dataset) {
            window_states_["file_browser"] = false;
            if (is_dataset) {
                lfs::core::events::cmd::ShowDatasetLoadPopup{.dataset_path = path}.emit();
            } else {
                lfs::core::events::cmd::LoadFile{.path = path, .is_dataset = false}.emit();
            }
        });

        initMenuBar();
        menu_bar_->setFonts(buildFontSet());

        // Load startup overlay textures
        const auto loadOverlayTexture = [](const std::filesystem::path& path, unsigned int& tex, int& w, int& h) {
            try {
                const auto [data, width, height, channels] = lfs::core::load_image_with_alpha(path);
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             channels == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, data);
                lfs::core::free_image(data);
                glBindTexture(GL_TEXTURE_2D, 0);
                w = width;
                h = height;
            } catch (const std::exception& e) {
                LOG_WARN("Failed to load overlay texture {}: {}", lfs::core::path_to_utf8(path), e.what());
            }
        };
        loadOverlayTexture(lfs::vis::getAssetPath("lichtfeld-splash-logo.png"),
                           startup_logo_light_texture_, startup_logo_width_, startup_logo_height_);
        loadOverlayTexture(lfs::vis::getAssetPath("lichtfeld-splash-logo-dark.png"),
                           startup_logo_dark_texture_, startup_logo_width_, startup_logo_height_);
        loadOverlayTexture(lfs::vis::getAssetPath("core11-logo.png"),
                           startup_core11_light_texture_, startup_core11_width_, startup_core11_height_);
        loadOverlayTexture(lfs::vis::getAssetPath("core11-logo-dark.png"),
                           startup_core11_dark_texture_, startup_core11_width_, startup_core11_height_);

        if (!drag_drop_.init(viewer_->getWindow())) {
            LOG_WARN("Native drag-drop initialization failed, falling back to GLFW");
        }
        drag_drop_.setFileDropCallback([this](const std::vector<std::string>& paths) {
            LOG_INFO("Files dropped via native drag-drop: {} file(s)", paths.size());
            if (auto* const ic = viewer_->getInputController()) {
                ic->handleFileDrop(paths);
            } else {
                LOG_ERROR("InputController not available for file drop handling");
            }
        });
    }

    void GuiManager::shutdown() {
        drag_drop_.shutdown();

        if (startup_logo_light_texture_)
            glDeleteTextures(1, &startup_logo_light_texture_);
        if (startup_logo_dark_texture_)
            glDeleteTextures(1, &startup_logo_dark_texture_);
        if (startup_core11_light_texture_)
            glDeleteTextures(1, &startup_core11_light_texture_);
        if (startup_core11_dark_texture_)
            glDeleteTextures(1, &startup_core11_dark_texture_);

        if (ImGui::GetCurrentContext()) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
        }
    }

    void GuiManager::render() {
        drag_drop_.pollEvents();
        drag_drop_hovering_ = drag_drop_.isDragHovering();

        // Start frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // Check mouse state before ImGui::NewFrame() updates WantCaptureMouse
        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool mouse_in_viewport = isPositionInViewport(mouse_pos.x, mouse_pos.y);

        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            ImGui::ClearActiveID();
            if (auto* editor = panels::PythonConsoleState::getInstance().getEditor()) {
                editor->unfocus();
            }
        }

        // Check for async import completion (must happen on main thread)
        checkAsyncImportCompletion();

        // Poll UV package manager for async operations
        python::PackageManager::instance().poll();

        // Hot-reload themes (check once per second)
        {
            static auto last_check = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - last_check > std::chrono::seconds(1)) {
                checkThemeFileChanges();
                last_check = now;
            }
        }

        // Initialize ImGuizmo for this frame
        ImGuizmo::BeginFrame();

        if (menu_bar_ && !ui_hidden_) {
            menu_bar_->render();
        }

        // Check for popups/modals and text input state - used to prevent input overrides
        // when UI elements like popups or text fields should receive input
        const bool any_popup_or_modal_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool imgui_wants_input = ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard;

        // Override ImGui's mouse capture for gizmo interaction
        // If ImGuizmo is being used or hovered, let it handle the mouse
        // But not if a popup/modal is open - those should take priority for input
        if ((ImGuizmo::IsOver() || ImGuizmo::IsUsing()) && !any_popup_or_modal_open) {
            ImGui::GetIO().WantCaptureMouse = false;
            ImGui::GetIO().WantCaptureKeyboard = false;
        }

        // Override ImGui's mouse capture for right/middle buttons when in viewport
        // This ensures that camera controls work properly
        // Skip if any popup/modal is open or ImGui wants text input (e.g., input fields are focused)
        if (mouse_in_viewport && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
            !any_popup_or_modal_open && !imgui_wants_input) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                ImGui::GetIO().WantCaptureMouse = false;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::ClearActiveID();
                ImGui::GetIO().WantCaptureKeyboard = false;
                if (auto* editor = panels::PythonConsoleState::getInstance().getEditor()) {
                    editor->unfocus();
                }
            }
        }

        // In point cloud mode, disable ImGui mouse capture in viewport
        auto* rendering_manager = viewer_->getRenderingManager();
        if (rendering_manager) {
            const auto& settings = rendering_manager->getSettings();
            if (settings.point_cloud_mode && mouse_in_viewport &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
                !any_popup_or_modal_open && !imgui_wants_input) {
                ImGui::GetIO().WantCaptureMouse = false;
                ImGui::GetIO().WantCaptureKeyboard = false;
            }
        }

        // Create main dockspace
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(main_viewport->WorkSize);
        ImGui::SetNextWindowViewport(main_viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoNavFocus |
                                        ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        // DockSpace ID
        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

        // Create dockspace
        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        ImGui::End();

        // Update editor context state for this frame
        auto& editor_ctx = viewer_->getEditorContext();
        editor_ctx.update(viewer_->getSceneManager(), viewer_->getTrainerManager());

        // Create context for this frame
        UIContext ctx{
            .viewer = viewer_,
            .file_browser = file_browser_.get(),
            .window_states = &window_states_,
            .editor = &editor_ctx,
            .sequencer_controller = &sequencer_controller_,
            .fonts = buildFontSet()};

        // Right panel and docked Python console
        if (show_main_panel_ && !ui_hidden_) {
            const auto* const vp = ImGui::GetMainViewport();
            const float panel_h = vp->WorkSize.y - STATUS_BAR_HEIGHT;
            const float min_w = vp->WorkSize.x * RIGHT_PANEL_MIN_RATIO;
            const float max_w = vp->WorkSize.x * RIGHT_PANEL_MAX_RATIO;
            constexpr float PANEL_GAP = 2.0f;

            // on windows, when window is minimized, WorkSize can be zero
            if (min_w != 0 || max_w != 0)
                right_panel_width_ = std::clamp(right_panel_width_, min_w, max_w);

            // Calculate available space for viewport + Python console
            const bool python_console_visible = window_states_["python_console"];
            const float available_for_split = vp->WorkSize.x - right_panel_width_ - PANEL_GAP;

            // Initialize Python console width to 1:1 split if not set or too small
            if (python_console_visible && python_console_width_ < 0.0f) {
                // First time opening: 1:1 split
                python_console_width_ = (available_for_split - PANEL_GAP) / 2.0f;
            }

            // Clamp Python console width
            if (python_console_visible) {
                const float max_console_w = available_for_split - PYTHON_CONSOLE_MIN_WIDTH;
                python_console_width_ = std::clamp(python_console_width_, PYTHON_CONSOLE_MIN_WIDTH, max_console_w);
            }

            // Right panel position
            const float right_panel_x = vp->WorkPos.x + vp->WorkSize.x - right_panel_width_;

            // Python console position (between viewport and right panel)
            const float console_x = right_panel_x - (python_console_visible ? python_console_width_ + PANEL_GAP : 0.0f);

            // Render docked Python console
            if (python_console_visible) {
                renderDockedPythonConsole(ctx, console_x, panel_h);
            } else {
                python_console_hovering_edge_ = false;
                python_console_resizing_ = false;
            }

            const float panel_x = right_panel_x;
            ImGui::SetNextWindowPos({panel_x, vp->WorkPos.y}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({right_panel_width_, panel_h}, ImGuiCond_Always);

            constexpr ImGuiWindowFlags PANEL_FLAGS =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoTitleBar;

            const auto& t = theme();
            ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(t.palette.surface, 0.95f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});

            // Left edge resize handle
            constexpr float EDGE_GRAB_W = 8.0f;
            const auto& io = ImGui::GetIO();
            hovering_panel_edge_ = io.MousePos.x >= panel_x - EDGE_GRAB_W &&
                                   io.MousePos.x <= panel_x + EDGE_GRAB_W &&
                                   io.MousePos.y >= vp->WorkPos.y &&
                                   io.MousePos.y <= vp->WorkPos.y + panel_h;

            if (hovering_panel_edge_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                resizing_panel_ = true;
            if (resizing_panel_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                resizing_panel_ = false;
            if (resizing_panel_) {
                right_panel_width_ = std::clamp(right_panel_width_ - io.MouseDelta.x, min_w, max_w);
                updateViewportRegion();
            }
            if (hovering_panel_edge_ || resizing_panel_)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            if (ImGui::Begin("##RightPanel", nullptr, PANEL_FLAGS)) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, {0, 0, 0, 0});
                auto* const sm = ctx.viewer->getSceneManager();
                lfs::vis::Scene* scene = sm ? &sm->getScene() : nullptr;

                const float avail_h = ImGui::GetContentRegionAvail().y;
                const float dpi = lfs::python::get_shared_dpi_scale();
                constexpr float SPLITTER_H = 6.0f;
                constexpr float MIN_H = 80.0f;
                const float splitter_h = SPLITTER_H * dpi;
                const float min_h = MIN_H * dpi;

                // Scene panel (top section)
                const float scene_h = std::max(min_h, avail_h * scene_panel_ratio_ - splitter_h * 0.5f);
                if (ImGui::BeginChild("##ScenePanel", {0, scene_h}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
                    python::draw_python_panels(python::PanelSpace::SceneHeader, scene);
                }
                ImGui::EndChild();

                // Splitter
                const auto& t = vis::theme();
                ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(t.palette.border, 0.4f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(t.palette.info, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(t.palette.info, 0.8f));
                ImGui::Button("##SceneSplitter", {-1, splitter_h});
                if (ImGui::IsItemActive()) {
                    scene_panel_ratio_ = std::clamp(scene_panel_ratio_ + ImGui::GetIO().MouseDelta.y / avail_h, 0.15f, 0.85f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                }
                ImGui::PopStyleColor(3);

                // Main panel tabs (bottom section)
                const auto main_tabs = python::get_main_panel_tabs();
                if (ImGui::BeginTabBar("##MainPanelTabs")) {
                    for (const auto& tab : main_tabs) {
                        ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                        if (focus_panel_name_ == tab.label || focus_panel_name_ == tab.idname) {
                            flags = ImGuiTabItemFlags_SetSelected;
                            focus_panel_name_.clear();
                        }
                        const std::string tab_label = tab.label + "##" + tab.idname;
                        if (ImGui::BeginTabItem(tab_label.c_str(), nullptr, flags)) {
                            const std::string child_id = "##" + tab.idname + "Panel";
                            if (ImGui::BeginChild(child_id.c_str(), {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
                                python::draw_main_panel_tab(tab.idname, scene);
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                }
                ImGui::PopStyleColor();
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        } else {
            hovering_panel_edge_ = false;
            resizing_panel_ = false;
            python_console_hovering_edge_ = false;
            python_console_resizing_ = false;
        }

        // Render floating windows
        if (window_states_["file_browser"]) {
            file_browser_->render(&window_states_["file_browser"]);
        }

        // Video extractor dialog
        if (window_states_["video_extractor_dialog"]) {
            video_extractor_dialog_->render(&window_states_["video_extractor_dialog"]);
        }

        python::set_viewport_bounds(viewport_pos_.x, viewport_pos_.y, viewport_size_.x, viewport_size_.y);
        renderPythonPanels(ctx);

        // Tool state updates (only when node selected and UI visible)
        auto* const scene_manager = ctx.viewer->getSceneManager();
        if (scene_manager && scene_manager->hasSelectedNode() && !ui_hidden_) {
            const auto& active_tool_id = UnifiedToolRegistry::instance().getActiveTool();
            const auto& gizmo_type = ctx.editor->getGizmoType();

            bool is_transform_tool = false;
            if (!gizmo_type.empty()) {
                is_transform_tool = true;
                if (gizmo_type == "translate") {
                    node_gizmo_operation_ = ImGuizmo::TRANSLATE;
                    current_operation_ = ImGuizmo::TRANSLATE;
                } else if (gizmo_type == "rotate") {
                    node_gizmo_operation_ = ImGuizmo::ROTATE;
                    current_operation_ = ImGuizmo::ROTATE;
                } else if (gizmo_type == "scale") {
                    node_gizmo_operation_ = ImGuizmo::SCALE;
                    current_operation_ = ImGuizmo::SCALE;
                } else {
                    is_transform_tool = false;
                }
            } else if (active_tool_id == "builtin.translate" || active_tool_id == "builtin.rotate" ||
                       active_tool_id == "builtin.scale") {
                is_transform_tool = true;
                node_gizmo_operation_ = current_operation_;
            }
            show_node_gizmo_ = is_transform_tool;

            auto* const brush_tool = ctx.viewer->getBrushTool();
            auto* const align_tool = ctx.viewer->getAlignTool();
            auto* const selection_tool = ctx.viewer->getSelectionTool();
            const bool is_brush_mode = (active_tool_id == "builtin.brush");
            const bool is_align_mode = (active_tool_id == "builtin.align");
            const bool is_selection_mode = (active_tool_id == "builtin.select");

            if (previous_tool_id_ == "builtin.select" && active_tool_id != previous_tool_id_) {
                if (auto* const sm = ctx.viewer->getSceneManager()) {
                    sm->applyDeleted();
                }
            }
            previous_tool_id_ = active_tool_id;

            if (brush_tool)
                brush_tool->setEnabled(is_brush_mode);
            if (align_tool)
                align_tool->setEnabled(is_align_mode);
            if (selection_tool)
                selection_tool->setEnabled(is_selection_mode);

            if (is_selection_mode) {
                if (auto* const rm = ctx.viewer->getRenderingManager()) {
                    auto mode = lfs::rendering::SelectionMode::Centers;
                    switch (selection_mode_) {
                    case SelectionSubMode::Centers: mode = lfs::rendering::SelectionMode::Centers; break;
                    case SelectionSubMode::Rectangle: mode = lfs::rendering::SelectionMode::Rectangle; break;
                    case SelectionSubMode::Polygon: mode = lfs::rendering::SelectionMode::Polygon; break;
                    case SelectionSubMode::Lasso: mode = lfs::rendering::SelectionMode::Lasso; break;
                    case SelectionSubMode::Rings: mode = lfs::rendering::SelectionMode::Rings; break;
                    }
                    rm->setSelectionMode(mode);

                    if (selection_mode_ != previous_selection_mode_) {
                        if (selection_tool)
                            selection_tool->onSelectionModeChanged();

                        if (selection_mode_ == SelectionSubMode::Rings) {
                            auto settings = rm->getSettings();
                            settings.show_rings = true;
                            settings.show_center_markers = false;
                            rm->updateSettings(settings);
                        }
                        previous_selection_mode_ = selection_mode_;
                    }
                }
            }

        } else {
            show_node_gizmo_ = false;
            if (auto* const tool = ctx.viewer->getBrushTool())
                tool->setEnabled(false);
            if (auto* const tool = ctx.viewer->getAlignTool())
                tool->setEnabled(false);
            if (auto* const tool = ctx.viewer->getSelectionTool())
                tool->setEnabled(false);
        }

        if (auto* const tool = ctx.viewer->getBrushTool(); tool && tool->isEnabled() && !ui_hidden_) {
            tool->renderUI(ctx, nullptr);
        }
        if (auto* const tool = ctx.viewer->getSelectionTool(); tool && tool->isEnabled() && !ui_hidden_) {
            tool->renderUI(ctx, nullptr);
        }

        // Selection tool overlays - only draw when mouse is in viewport (not over UI)
        const bool mouse_over_ui = ImGui::GetIO().WantCaptureMouse;
        if (!ui_hidden_ && !mouse_over_ui && viewport_size_.x > 0 && viewport_size_.y > 0) {
            auto* rm = ctx.viewer->getRenderingManager();
            auto* draw_list = ImGui::GetForegroundDrawList();

            // Brush circle
            if (rm && rm->isBrushActive()) {
                const auto& t = theme();

                // Get brush state from rendering manager
                float bx, by, br;
                bool add_mode;
                rm->getBrushState(bx, by, br, add_mode);

                // Convert from render coordinates to screen coordinates
                // Brush state is in render coords: render = (screen - viewport) * render_scale
                // So: screen = render / render_scale + viewport
                const float render_scale = rm->getSettings().render_scale;
                const ImVec2 screen_pos(viewport_pos_.x + bx / render_scale,
                                        viewport_pos_.y + by / render_scale);
                const float screen_radius = br / render_scale;

                // Draw brush circle
                const ImU32 brush_color = add_mode
                                              ? toU32WithAlpha(t.palette.success, 0.8f)
                                              : toU32WithAlpha(t.palette.error, 0.8f);
                draw_list->AddCircle(screen_pos, screen_radius, brush_color, 32, 2.0f);
                draw_list->AddCircleFilled(screen_pos, 3.0f, brush_color);
            }

            // Rectangle selection preview
            if (rm && rm->isRectPreviewActive()) {
                const auto& t = theme();

                float rx0, ry0, rx1, ry1;
                bool add_mode;
                rm->getRectPreview(rx0, ry0, rx1, ry1, add_mode);

                const float render_scale = rm->getSettings().render_scale;
                const ImVec2 p0(viewport_pos_.x + rx0 / render_scale, viewport_pos_.y + ry0 / render_scale);
                const ImVec2 p1(viewport_pos_.x + rx1 / render_scale, viewport_pos_.y + ry1 / render_scale);

                const ImU32 fill_color = add_mode
                                             ? toU32WithAlpha(t.palette.success, 0.15f)
                                             : toU32WithAlpha(t.palette.error, 0.15f);
                const ImU32 border_color = add_mode
                                               ? toU32WithAlpha(t.palette.success, 0.8f)
                                               : toU32WithAlpha(t.palette.error, 0.8f);

                draw_list->AddRectFilled(p0, p1, fill_color);
                draw_list->AddRect(p0, p1, border_color, 0.0f, 0, 2.0f);
            }

            // Polygon selection preview
            if (rm && rm->isPolygonPreviewActive()) {
                const auto& t = theme();

                const auto& points = rm->getPolygonPoints();
                const bool closed = rm->isPolygonClosed();
                const bool add_mode = rm->isPolygonAddMode();

                if (!points.empty()) {
                    const float render_scale = rm->getSettings().render_scale;
                    const ImU32 line_color = add_mode
                                                 ? toU32WithAlpha(t.palette.success, 0.8f)
                                                 : toU32WithAlpha(t.palette.error, 0.8f);
                    const ImU32 fill_color = add_mode
                                                 ? toU32WithAlpha(t.palette.success, 0.15f)
                                                 : toU32WithAlpha(t.palette.error, 0.15f);
                    const ImU32 vertex_color = add_mode
                                                   ? toU32WithAlpha(t.palette.success, 1.0f)
                                                   : toU32WithAlpha(t.palette.error, 1.0f);
                    const ImU32 line_to_mouse_color = add_mode
                                                          ? toU32WithAlpha(t.palette.success, 0.5f)
                                                          : toU32WithAlpha(t.palette.error, 0.5f);

                    // Build screen-space points
                    std::vector<ImVec2> screen_points;
                    screen_points.reserve(points.size());
                    for (const auto& [px, py] : points) {
                        screen_points.emplace_back(viewport_pos_.x + px / render_scale,
                                                   viewport_pos_.y + py / render_scale);
                    }

                    // Draw filled polygon if closed
                    if (closed && screen_points.size() >= 3) {
                        draw_list->AddConvexPolyFilled(screen_points.data(), static_cast<int>(screen_points.size()), fill_color);
                    }

                    // Draw lines between vertices
                    for (size_t i = 0; i + 1 < screen_points.size(); ++i) {
                        draw_list->AddLine(screen_points[i], screen_points[i + 1], line_color, 2.0f);
                    }
                    if (closed && screen_points.size() >= 3) {
                        draw_list->AddLine(screen_points.back(), screen_points.front(), line_color, 2.0f);
                    }

                    // Draw line from last vertex to mouse when not closed
                    if (!closed) {
                        const ImVec2 mouse_pos = ImGui::GetMousePos();
                        draw_list->AddLine(screen_points.back(), mouse_pos, line_to_mouse_color, 1.0f);

                        // Draw close hint when near first vertex
                        constexpr float CLOSE_THRESHOLD = 12.0f;
                        if (screen_points.size() >= 3) {
                            const float dx = mouse_pos.x - screen_points.front().x;
                            const float dy = mouse_pos.y - screen_points.front().y;
                            if (dx * dx + dy * dy < CLOSE_THRESHOLD * CLOSE_THRESHOLD) {
                                draw_list->AddCircle(screen_points.front(), 9.0f, vertex_color, 16, 2.0f);
                            }
                        }
                    }

                    // Draw vertices
                    for (const auto& sp : screen_points) {
                        draw_list->AddCircleFilled(sp, 5.0f, vertex_color);
                    }
                }
            }

            // Lasso selection preview
            if (rm && rm->isLassoPreviewActive()) {
                const auto& t = theme();

                const auto& points = rm->getLassoPoints();
                const bool add_mode = rm->isLassoAddMode();

                if (points.size() >= 2) {
                    const float render_scale = rm->getSettings().render_scale;
                    const ImU32 line_color = add_mode
                                                 ? toU32WithAlpha(t.palette.success, 0.8f)
                                                 : toU32WithAlpha(t.palette.error, 0.8f);

                    ImVec2 prev(viewport_pos_.x + points[0].first / render_scale,
                                viewport_pos_.y + points[0].second / render_scale);
                    for (size_t i = 1; i < points.size(); ++i) {
                        ImVec2 curr(viewport_pos_.x + points[i].first / render_scale,
                                    viewport_pos_.y + points[i].second / render_scale);
                        draw_list->AddLine(prev, curr, line_color, 2.0f);
                        prev = curr;
                    }
                }
            }
        }

        auto* align_tool = ctx.viewer->getAlignTool();
        if (align_tool && align_tool->isEnabled() && !ui_hidden_) {
            align_tool->renderUI(ctx, nullptr);
        }

        // Node selection rectangle
        if (auto* const ic = ctx.viewer->getInputController();
            !ui_hidden_ && ic && ic->isNodeRectDragging()) {
            const auto start = ic->getNodeRectStart();
            const auto end = ic->getNodeRectEnd();
            const auto& t = theme();
            auto* const draw_list = ImGui::GetForegroundDrawList();
            draw_list->AddRectFilled({start.x, start.y}, {end.x, end.y}, toU32WithAlpha(t.palette.warning, 0.15f));
            draw_list->AddRect({start.x, start.y}, {end.x, end.y}, toU32WithAlpha(t.palette.warning, 0.85f), 0.0f, 0, 2.0f);
        }

        // Node gizmo first: parent transforms must update before children read them
        renderNodeTransformGizmo(ctx);
        renderCropBoxGizmo(ctx);
        renderEllipsoidGizmo(ctx);

        updateCropFlash();

        // Get the viewport region for 3D rendering
        updateViewportRegion();

        // Update viewport focus based on mouse position
        updateViewportFocus();

        // Draw vignette effect on viewport
        if (viewport_size_.x > 0 && viewport_size_.y > 0) {
            widgets::DrawViewportVignette(viewport_pos_, viewport_size_);
        }

        // Mask viewport corners with background for rounded effect
        if (!ui_hidden_ && viewport_size_.x > 0 && viewport_size_.y > 0) {
            const auto& t = theme();
            const float r = t.viewport.corner_radius;
            if (r > 0.0f) {
                auto* const dl = ImGui::GetBackgroundDrawList();
                const ImU32 bg = toU32(t.palette.background);
                const float x1 = viewport_pos_.x, y1 = viewport_pos_.y;
                const float x2 = x1 + viewport_size_.x, y2 = y1 + viewport_size_.y;

                // Draw corner wedge: corner -> edge -> arc -> corner
                constexpr int CORNER_ARC_SEGMENTS = 12;
                const auto maskCorner = [&](const ImVec2 corner, const ImVec2 edge,
                                            const ImVec2 center, const float a0, const float a1) {
                    dl->PathLineTo(corner);
                    dl->PathLineTo(edge);
                    dl->PathArcTo(center, r, a0, a1, CORNER_ARC_SEGMENTS);
                    dl->PathLineTo(corner);
                    dl->PathFillConvex(bg);
                };
                maskCorner({x1, y1}, {x1, y1 + r}, {x1 + r, y1 + r}, IM_PI, IM_PI * 1.5f);
                maskCorner({x2, y1}, {x2 - r, y1}, {x2 - r, y1 + r}, IM_PI * 1.5f, IM_PI * 2.0f);
                maskCorner({x1, y2}, {x1 + r, y2}, {x1 + r, y2 - r}, IM_PI * 0.5f, IM_PI);
                maskCorner({x2, y2}, {x2, y2 - r}, {x2 - r, y2 - r}, 0.0f, IM_PI * 0.5f);

                if (t.viewport.border_size > 0.0f) {
                    dl->AddRect({x1, y1}, {x2, y2}, t.viewport_border_u32(), r,
                                ImDrawFlags_RoundCornersAll, t.viewport.border_size);
                }
            }
        }

        // Sequencer panel (above status bar)
        if (!ui_hidden_ && ctx.editor && !ctx.editor->isToolsDisabled() && show_sequencer_) {
            if (sequencer_ui_state_.show_camera_path) {
                renderCameraPath(ctx);
                renderKeyframeGizmo(ctx);
                renderKeyframePreview(ctx);
            }
            renderSequencerPanel(ctx);
            drawPipPreviewWindow(ctx);
        }

        // Render status bar at bottom of viewport
        if (!ui_hidden_) {
            lfs::vis::Scene* status_bar_scene = nullptr;
            if (auto* sm = ctx.viewer->getSceneManager()) {
                status_bar_scene = &sm->getScene();
            }
            python::draw_python_panels(python::PanelSpace::StatusBar, status_bar_scene);
        }

        // Render viewport gizmo and handle drag-to-orbit
        if (show_viewport_gizmo_ && !ui_hidden_ && viewport_size_.x > 0 && viewport_size_.y > 0) {
            if (rendering_manager) {
                if (auto* const engine = rendering_manager->getRenderingEngine()) {
                    auto& viewport = viewer_->getViewport();
                    const glm::vec2 vp_pos(viewport_pos_.x, viewport_pos_.y);
                    const glm::vec2 vp_size(viewport_size_.x, viewport_size_.y);

                    // Gizmo bounds (upper-right corner)
                    const float gizmo_x = vp_pos.x + vp_size.x - VIEWPORT_GIZMO_SIZE - VIEWPORT_GIZMO_MARGIN_X;
                    const float gizmo_y = vp_pos.y + VIEWPORT_GIZMO_MARGIN_Y;

                    const ImVec2 mouse = ImGui::GetMousePos();
                    const bool mouse_in_gizmo = mouse.x >= gizmo_x && mouse.x <= gizmo_x + VIEWPORT_GIZMO_SIZE &&
                                                mouse.y >= gizmo_y && mouse.y <= gizmo_y + VIEWPORT_GIZMO_SIZE;

                    const int hovered_axis = engine->hitTestViewportGizmo(glm::vec2(mouse.x, mouse.y), vp_pos, vp_size);
                    engine->setViewportGizmoHover(hovered_axis);

                    if (!ImGui::GetIO().WantCaptureMouse) {
                        const glm::vec2 capture_mouse_pos(mouse.x, mouse.y);
                        const float time = static_cast<float>(ImGui::GetTime());

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_gizmo) {
                            if (hovered_axis >= 0 && hovered_axis <= 5) {
                                // Snap to axis view
                                const int axis = hovered_axis % 3;
                                const bool negative = hovered_axis >= 3;
                                const glm::mat3 rotation = engine->getAxisViewRotation(axis, negative);
                                const float dist = glm::length(viewport.camera.pivot - viewport.camera.t);

                                viewport.camera.pivot = glm::vec3(0.0f);
                                viewport.camera.R = rotation;
                                viewport.camera.t = -rotation[2] * dist;

                                const auto& settings = rendering_manager->getSettings();
                                lfs::core::events::ui::GridSettingsChanged{
                                    .enabled = settings.show_grid,
                                    .plane = axis,
                                    .opacity = settings.grid_opacity}
                                    .emit();

                                rendering_manager->markDirty();
                            } else {
                                // Drag to orbit
                                viewport_gizmo_dragging_ = true;
                                viewport.camera.startRotateAroundCenter(capture_mouse_pos, time);
                                if (GLFWwindow* const window = glfwGetCurrentContext()) {
                                    glfwGetCursorPos(window, &gizmo_drag_start_cursor_.x, &gizmo_drag_start_cursor_.y);
                                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                                }
                            }
                        }

                        if (viewport_gizmo_dragging_) {
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                                viewport.camera.updateRotateAroundCenter(capture_mouse_pos, time);
                                rendering_manager->markDirty();
                            } else {
                                viewport.camera.endRotateAroundCenter();
                                viewport_gizmo_dragging_ = false;

                                // Release cursor, restore position
                                if (GLFWwindow* const window = glfwGetCurrentContext()) {
                                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                                    glfwSetCursorPos(window, gizmo_drag_start_cursor_.x, gizmo_drag_start_cursor_.y);
                                }
                            }
                        }
                    }

                    if (auto result = engine->renderViewportGizmo(viewport.getRotationMatrix(), vp_pos, vp_size); !result) {
                        LOG_WARN("Failed to render viewport gizmo: {}", result.error());
                    }

                    // Drag feedback overlay
                    if (viewport_gizmo_dragging_) {
                        const float center_x = gizmo_x + VIEWPORT_GIZMO_SIZE * 0.5f;
                        const float center_y = gizmo_y + VIEWPORT_GIZMO_SIZE * 0.5f;
                        constexpr float OVERLAY_RADIUS = VIEWPORT_GIZMO_SIZE * 0.46f; // Match gizmo content + 2px
                        ImGui::GetBackgroundDrawList()->AddCircleFilled(
                            ImVec2(center_x, center_y), OVERLAY_RADIUS,
                            toU32WithAlpha(theme().overlay.text_dim, 0.2f), 32);
                    }
                }
            }
        }

        renderStartupOverlay();

        // Render keyframe context menu (needs to be at end of frame for proper z-order)
        if (keyframe_context_menu_open_) {
            const auto& timeline = sequencer_controller_.timeline();
            if (ImGui::BeginPopup("KeyframeContextMenu")) {
                if (ImGui::MenuItem("Add Keyframe Here", "K")) {
                    lfs::core::events::cmd::SequencerAddKeyframe{}.emit();
                }
                if (context_menu_keyframe_.has_value() && *context_menu_keyframe_ < timeline.size()) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Update to Current View", "U")) {
                        sequencer_controller_.selectKeyframe(*context_menu_keyframe_);
                        lfs::core::events::cmd::SequencerUpdateKeyframe{}.emit();
                    }
                    if (ImGui::MenuItem("Go to Keyframe")) {
                        sequencer_controller_.selectKeyframe(*context_menu_keyframe_);
                        sequencer_controller_.seek(timeline.keyframes()[*context_menu_keyframe_].time);
                    }
                    ImGui::Separator();
                    const bool translate_active = keyframe_gizmo_op_ == ImGuizmo::TRANSLATE;
                    const bool rotate_active = keyframe_gizmo_op_ == ImGuizmo::ROTATE;
                    if (ImGui::MenuItem("Move (Translate)", nullptr, translate_active)) {
                        sequencer_controller_.selectKeyframe(*context_menu_keyframe_);
                        keyframe_gizmo_op_ = translate_active ? ImGuizmo::OPERATION(0) : ImGuizmo::TRANSLATE;
                    }
                    if (ImGui::MenuItem("Rotate", nullptr, rotate_active)) {
                        sequencer_controller_.selectKeyframe(*context_menu_keyframe_);
                        keyframe_gizmo_op_ = rotate_active ? ImGuizmo::OPERATION(0) : ImGuizmo::ROTATE;
                    }
                    ImGui::Separator();
                    // Easing submenu (only for non-last keyframes)
                    const size_t idx = *context_menu_keyframe_;
                    const bool is_last = (idx == timeline.size() - 1);
                    if (ImGui::BeginMenu("Easing", !is_last)) {
                        static constexpr const char* EASING_NAMES[] = {"Linear", "Ease In", "Ease Out", "Ease In-Out"};
                        const auto current_easing = timeline.keyframes()[idx].easing;
                        for (int e = 0; e < 4; ++e) {
                            const auto easing = static_cast<sequencer::EasingType>(e);
                            if (ImGui::MenuItem(EASING_NAMES[e], nullptr, current_easing == easing)) {
                                if (current_easing != easing) {
                                    sequencer_controller_.timeline().setKeyframeEasing(idx, easing);
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (is_last && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Easing controls outgoing motion\n(last keyframe has no outgoing segment)");
                    }
                    ImGui::Separator();
                    const bool is_first = (*context_menu_keyframe_ == 0);
                    if (ImGui::MenuItem("Delete Keyframe", "Del", false, !is_first)) {
                        sequencer_controller_.selectKeyframe(*context_menu_keyframe_);
                        sequencer_controller_.removeSelectedKeyframe();
                    }
                    if (is_first && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Cannot delete first keyframe");
                    }
                }
                ImGui::EndPopup();
            } else {
                keyframe_context_menu_open_ = false;
                context_menu_keyframe_ = std::nullopt;
            }
        }

        if (disk_space_error_dialog_)
            disk_space_error_dialog_->render();

        // Notification popups are rendered via PyModalRegistry (draw_modals in Python bridge)

        // End frame
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Clean up GL state after ImGui rendering (ImGui can leave VAO/shader bindings corrupted)
        glBindVertexArray(0);
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        // Clear any errors ImGui might have generated
        while (glGetError() != GL_NO_ERROR) {}

        // Update and Render additional Platform Windows (for multi-viewport)
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);

            // Clean up GL state after multi-viewport rendering too
            glBindVertexArray(0);
            glUseProgram(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            while (glGetError() != GL_NO_ERROR) {}
        }
    }

    void GuiManager::updateViewportRegion() {
        constexpr float PANEL_GAP = 2.0f;
        const auto* const vp = ImGui::GetMainViewport();

        // Calculate Python console width if visible (handle uninitialized state)
        float console_w = 0.0f;
        if (window_states_["python_console"] && show_main_panel_ && !ui_hidden_) {
            if (python_console_width_ < 0.0f) {
                // Not yet initialized, estimate 1:1 split
                const float available = vp->WorkSize.x - right_panel_width_ - PANEL_GAP;
                console_w = (available - PANEL_GAP) / 2.0f + PANEL_GAP;
            } else {
                console_w = python_console_width_ + PANEL_GAP;
            }
        }

        const float w = (show_main_panel_ && !ui_hidden_)
                            ? vp->WorkSize.x - right_panel_width_ - console_w - PANEL_GAP
                            : vp->WorkSize.x;
        const float h = ui_hidden_ ? vp->WorkSize.y : vp->WorkSize.y - STATUS_BAR_HEIGHT;
        viewport_pos_ = {vp->WorkPos.x, vp->WorkPos.y};
        viewport_size_ = {w, h};
    }

    void GuiManager::updateViewportFocus() {
        // Viewport has focus unless actively using a GUI widget
        viewport_has_focus_ = !ImGui::IsAnyItemActive();
    }

    ImVec2 GuiManager::getViewportPos() const {
        return viewport_pos_;
    }

    ImVec2 GuiManager::getViewportSize() const {
        return viewport_size_;
    }

    bool GuiManager::isMouseInViewport() const {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        return mouse_pos.x >= viewport_pos_.x &&
               mouse_pos.y >= viewport_pos_.y &&
               mouse_pos.x < viewport_pos_.x + viewport_size_.x &&
               mouse_pos.y < viewport_pos_.y + viewport_size_.y;
    }

    bool GuiManager::isViewportFocused() const {
        return viewport_has_focus_;
    }

    bool GuiManager::isPositionInViewport(double x, double y) const {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();

        // Convert to window-relative coordinates
        float rel_x = static_cast<float>(x) - main_viewport->WorkPos.x;
        float rel_y = static_cast<float>(y) - main_viewport->WorkPos.y;

        // Check if within viewport bounds
        return (rel_x >= viewport_pos_.x &&
                rel_x < viewport_pos_.x + viewport_size_.x &&
                rel_y >= viewport_pos_.y &&
                rel_y < viewport_pos_.y + viewport_size_.y);
    }

    bool GuiManager::isPositionInViewportGizmo(const double x, const double y) const {
        if (!show_viewport_gizmo_ || ui_hidden_)
            return false;

        const float gizmo_x = viewport_pos_.x + viewport_size_.x - VIEWPORT_GIZMO_SIZE - VIEWPORT_GIZMO_MARGIN_X;
        const float gizmo_y = viewport_pos_.y + VIEWPORT_GIZMO_MARGIN_Y;

        return x >= gizmo_x && x <= gizmo_x + VIEWPORT_GIZMO_SIZE &&
               y >= gizmo_y && y <= gizmo_y + VIEWPORT_GIZMO_SIZE;
    }

    void GuiManager::renderSequencerPanel(const UIContext& /*ctx*/) {
        sequencer_controller_.update(ImGui::GetIO().DeltaTime);

        const bool is_playing = sequencer_controller_.isPlaying() && !sequencer_controller_.timeline().empty();

        if (auto* const rm = viewer_->getRenderingManager()) {
            rm->setOverlayAnimationActive(is_playing);
            if (is_playing && sequencer_ui_state_.follow_playback) {
                rm->markDirty();
                const auto state = sequencer_controller_.currentCameraState();
                auto& viewport = viewer_->getViewport();
                viewport.camera.R = glm::mat3_cast(state.rotation);
                viewport.camera.t = state.position;
            }
        }

        sequencer_panel_->setSnapEnabled(sequencer_ui_state_.snap_to_grid);
        sequencer_panel_->setSnapInterval(sequencer_ui_state_.snap_interval);
        sequencer_panel_->render(viewport_pos_.x, viewport_size_.x, viewport_pos_.y + viewport_size_.y);
    }

    void GuiManager::renderCameraPath(const UIContext& /*ctx*/) {
        constexpr float PATH_THICKNESS = 2.0f;
        constexpr float FRUSTUM_THICKNESS = 1.5f;
        constexpr float NDC_CULL_MARGIN = 1.5f;
        constexpr int PATH_SAMPLES = 20;
        constexpr float FRUSTUM_SIZE = 0.15f;  // Size of frustum base
        constexpr float FRUSTUM_DEPTH = 0.25f; // Depth of frustum
        constexpr float HIT_RADIUS = 15.0f;    // Click detection radius in pixels

        const auto& timeline = sequencer_controller_.timeline();
        const auto& viewport = viewer_->getViewport();
        const glm::mat4 view_proj = viewport.getProjectionMatrix() * viewport.getViewMatrix();

        const auto projectToScreen = [&](const glm::vec3& pos) -> ImVec2 {
            const glm::vec4 clip = view_proj * glm::vec4(pos, 1.0f);
            if (clip.w <= 0.0f)
                return {-10000.0f, -10000.0f};
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {viewport_pos_.x + (ndc.x * 0.5f + 0.5f) * viewport_size_.x,
                    viewport_pos_.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewport_size_.y};
        };

        const auto isVisible = [&](const glm::vec3& pos) -> bool {
            const glm::vec4 clip = view_proj * glm::vec4(pos, 1.0f);
            if (clip.w <= 0.0f)
                return false;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return std::abs(ndc.x) <= NDC_CULL_MARGIN && std::abs(ndc.y) <= NDC_CULL_MARGIN;
        };

        ImDrawList* const dl = ImGui::GetBackgroundDrawList();
        const auto& t = theme();

        if (timeline.empty())
            return;

        // Path line
        const auto path_points = timeline.generatePath(PATH_SAMPLES);
        if (path_points.size() >= 2) {
            const ImU32 path_color = toU32WithAlpha(t.palette.primary, 0.8f);
            for (size_t i = 0; i + 1 < path_points.size(); ++i) {
                if (!isVisible(path_points[i]) && !isVisible(path_points[i + 1]))
                    continue;
                dl->AddLine(projectToScreen(path_points[i]), projectToScreen(path_points[i + 1]), path_color, PATH_THICKNESS);
            }
        }

        // Hit testing for keyframe clicking
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool mouse_in_viewport = mouse.x >= viewport_pos_.x && mouse.x <= viewport_pos_.x + viewport_size_.x &&
                                       mouse.y >= viewport_pos_.y && mouse.y <= viewport_pos_.y + viewport_size_.y;

        std::optional<size_t> hovered_keyframe;
        float closest_dist = HIT_RADIUS;

        // Draw frustums at keyframe positions
        const ImU32 frustum_color = toU32WithAlpha(t.palette.primary, 0.7f);
        const ImU32 hovered_frustum_color = toU32WithAlpha(lighten(t.palette.primary, 0.15f), 0.85f);
        const ImU32 selected_frustum_color = toU32WithAlpha(lighten(t.palette.primary, 0.3f), 0.9f);

        for (size_t i = 0; i < timeline.keyframes().size(); ++i) {
            const auto& kf = timeline.keyframes()[i];
            if (!isVisible(kf.position))
                continue;

            const ImVec2 s_apex = projectToScreen(kf.position);

            // Hit test
            if (mouse_in_viewport) {
                const float dist = std::sqrt((mouse.x - s_apex.x) * (mouse.x - s_apex.x) +
                                             (mouse.y - s_apex.y) * (mouse.y - s_apex.y));
                if (dist < closest_dist) {
                    closest_dist = dist;
                    hovered_keyframe = i;
                }
            }

            const bool selected = sequencer_controller_.selectedKeyframe() == i;
            const bool hovered = hovered_keyframe == i;
            ImU32 color = frustum_color;
            if (selected)
                color = selected_frustum_color;
            else if (hovered)
                color = hovered_frustum_color;
            const float thickness = selected ? FRUSTUM_THICKNESS * 1.5f : FRUSTUM_THICKNESS;

            // Build frustum in camera local space, then transform to world
            // Apply GL_TO_COLMAP transform (flip Y and Z) to match training camera frustums
            const glm::mat3 rot_mat = glm::mat3_cast(kf.rotation);
            const glm::vec3 forward = rot_mat[2]; // Z (GL_TO_COLMAP flips Z, was -Z)
            const glm::vec3 up = -rot_mat[1];     // -Y (GL_TO_COLMAP flips Y)
            const glm::vec3 right = rot_mat[0];   // X (unchanged)

            // Frustum apex at camera position
            const glm::vec3 apex = kf.position;

            // Frustum base corners (in front of camera)
            const glm::vec3 base_center = apex + forward * FRUSTUM_DEPTH;
            const glm::vec3 tl = base_center + up * FRUSTUM_SIZE - right * FRUSTUM_SIZE;
            const glm::vec3 tr = base_center + up * FRUSTUM_SIZE + right * FRUSTUM_SIZE;
            const glm::vec3 bl = base_center - up * FRUSTUM_SIZE - right * FRUSTUM_SIZE;
            const glm::vec3 br = base_center - up * FRUSTUM_SIZE + right * FRUSTUM_SIZE;

            // Project all points
            const ImVec2 s_tl = projectToScreen(tl);
            const ImVec2 s_tr = projectToScreen(tr);
            const ImVec2 s_bl = projectToScreen(bl);
            const ImVec2 s_br = projectToScreen(br);

            // Draw frustum edges (apex to corners)
            dl->AddLine(s_apex, s_tl, color, thickness);
            dl->AddLine(s_apex, s_tr, color, thickness);
            dl->AddLine(s_apex, s_bl, color, thickness);
            dl->AddLine(s_apex, s_br, color, thickness);

            // Draw base rectangle
            dl->AddLine(s_tl, s_tr, color, thickness);
            dl->AddLine(s_tr, s_br, color, thickness);
            dl->AddLine(s_br, s_bl, color, thickness);
            dl->AddLine(s_bl, s_tl, color, thickness);

            // Draw "up" indicator (small triangle on top edge)
            const glm::vec3 up_tip = base_center + up * FRUSTUM_SIZE * 1.3f;
            const ImVec2 s_up = projectToScreen(up_tip);
            dl->AddTriangleFilled(s_up, s_tl, s_tr, color);
        }

        // Handle keyframe clicking
        if (mouse_in_viewport && !ImGui::IsAnyItemHovered()) {
            // Left click to select (blocked when gizmo is active to avoid accidental selection)
            if (hovered_keyframe.has_value() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver()) {
                sequencer_controller_.selectKeyframe(*hovered_keyframe);
            }
            // Right click for context menu (always allowed, even with gizmo active)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                context_menu_keyframe_ = hovered_keyframe;
                keyframe_context_menu_open_ = true;
                ImGui::OpenPopup("KeyframeContextMenu");
            }
        }

        // Playhead camera frustum (during playback/scrubbing)
        if (!sequencer_controller_.isStopped()) {
            const auto state = sequencer_controller_.currentCameraState();
            if (isVisible(state.position)) {
                const ImU32 playhead_color = t.error_u32();
                constexpr float PLAYHEAD_FRUSTUM_SIZE = 0.12f;
                constexpr float PLAYHEAD_FRUSTUM_DEPTH = 0.20f;

                // Build frustum from interpolated camera state
                const glm::mat3 rot_mat = glm::mat3_cast(state.rotation);
                const glm::vec3 forward = rot_mat[2];
                const glm::vec3 up = -rot_mat[1];
                const glm::vec3 right = rot_mat[0];

                const glm::vec3 apex = state.position;
                const glm::vec3 base_center = apex + forward * PLAYHEAD_FRUSTUM_DEPTH;
                const glm::vec3 tl = base_center + up * PLAYHEAD_FRUSTUM_SIZE - right * PLAYHEAD_FRUSTUM_SIZE;
                const glm::vec3 tr = base_center + up * PLAYHEAD_FRUSTUM_SIZE + right * PLAYHEAD_FRUSTUM_SIZE;
                const glm::vec3 bl = base_center - up * PLAYHEAD_FRUSTUM_SIZE - right * PLAYHEAD_FRUSTUM_SIZE;
                const glm::vec3 br = base_center - up * PLAYHEAD_FRUSTUM_SIZE + right * PLAYHEAD_FRUSTUM_SIZE;

                const ImVec2 s_apex = projectToScreen(apex);
                const ImVec2 s_tl = projectToScreen(tl);
                const ImVec2 s_tr = projectToScreen(tr);
                const ImVec2 s_bl = projectToScreen(bl);
                const ImVec2 s_br = projectToScreen(br);

                // Draw frustum edges
                dl->AddLine(s_apex, s_tl, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_apex, s_tr, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_apex, s_bl, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_apex, s_br, playhead_color, FRUSTUM_THICKNESS);

                // Draw base rectangle
                dl->AddLine(s_tl, s_tr, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_tr, s_br, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_br, s_bl, playhead_color, FRUSTUM_THICKNESS);
                dl->AddLine(s_bl, s_tl, playhead_color, FRUSTUM_THICKNESS);

                // Draw "up" indicator
                const glm::vec3 up_tip = base_center + up * PLAYHEAD_FRUSTUM_SIZE * 1.3f;
                const ImVec2 s_up = projectToScreen(up_tip);
                dl->AddTriangleFilled(s_up, s_tl, s_tr, playhead_color);
            }
        }
    }

    void GuiManager::renderKeyframeGizmo(const UIContext& ctx) {
        if (keyframe_gizmo_op_ == ImGuizmo::OPERATION(0))
            return;

        const auto selected = sequencer_controller_.selectedKeyframe();
        if (!selected.has_value()) {
            keyframe_gizmo_op_ = ImGuizmo::OPERATION(0);
            return;
        }

        const auto& timeline = sequencer_controller_.timeline();
        if (*selected >= timeline.size())
            return;

        const auto* kf = timeline.getKeyframe(*selected);
        if (!kf || kf->is_loop_point) {
            keyframe_gizmo_op_ = ImGuizmo::OPERATION(0);
            return;
        }

        auto* const rendering_manager = ctx.viewer->getRenderingManager();
        if (!rendering_manager)
            return;

        const auto& settings = rendering_manager->getSettings();
        auto& viewport = ctx.viewer->getViewport();
        const glm::mat4 view = viewport.getViewMatrix();
        const glm::ivec2 vp_size(static_cast<int>(viewport_size_.x), static_cast<int>(viewport_size_.y));
        const glm::mat4 projection = lfs::rendering::createProjectionMatrix(
            vp_size, lfs::rendering::focalLengthToVFov(settings.focal_length_mm), settings.orthographic, settings.ortho_scale);

        const glm::mat3 rot_mat = glm::mat3_cast(kf->rotation);
        glm::mat4 gizmo_matrix(rot_mat);
        gizmo_matrix[3] = glm::vec4(kf->position, 1.0f);

        ImGuizmo::SetOrthographic(settings.orthographic);
        ImGuizmo::SetRect(viewport_pos_.x, viewport_pos_.y, viewport_size_.x, viewport_size_.y);

        ImDrawList* const dl = ImGui::GetForegroundDrawList();
        const ImVec2 clip_min(viewport_pos_.x, viewport_pos_.y);
        const ImVec2 clip_max(clip_min.x + viewport_size_.x, clip_min.y + viewport_size_.y);
        dl->PushClipRect(clip_min, clip_max, true);
        ImGuizmo::SetDrawlist(dl);

        const ImGuizmo::MODE mode = (keyframe_gizmo_op_ == ImGuizmo::ROTATE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        glm::mat4 delta;
        const bool changed = ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            keyframe_gizmo_op_, mode,
            glm::value_ptr(gizmo_matrix), glm::value_ptr(delta), nullptr);

        const bool is_using = ImGuizmo::IsUsing();

        if (is_using && !keyframe_gizmo_active_) {
            keyframe_gizmo_active_ = true;
            keyframe_pos_before_drag_ = kf->position;
            keyframe_rot_before_drag_ = kf->rotation;
        }

        if (changed) {
            const glm::vec3 new_pos(gizmo_matrix[3]);
            const glm::quat new_rot = glm::quat_cast(glm::mat3(gizmo_matrix));
            sequencer_controller_.timeline().updateKeyframe(*selected, new_pos, new_rot, kf->fov);
            sequencer_controller_.updateLoopKeyframe();
            pip_needs_update_ = true;
        }

        if (!is_using && keyframe_gizmo_active_) {
            keyframe_gizmo_active_ = false;
        }

        dl->PopClipRect();
    }

    void GuiManager::initPipPreview() {
        if (pip_initialized_)
            return;

        glGenFramebuffers(1, &pip_fbo_);
        glGenTextures(1, &pip_texture_);
        glGenRenderbuffers(1, &pip_depth_rbo_);

        glBindTexture(GL_TEXTURE_2D, pip_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, pip_depth_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, PREVIEW_WIDTH, PREVIEW_HEIGHT);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, pip_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pip_texture_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pip_depth_rbo_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERROR("PiP preview FBO incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        pip_initialized_ = true;
    }

    void GuiManager::renderKeyframePreview(const UIContext& ctx) {
        const bool is_playing = !sequencer_controller_.isStopped();
        const auto selected = sequencer_controller_.selectedKeyframe();

        if (!is_playing && !selected.has_value()) {
            pip_last_keyframe_ = std::nullopt;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (is_playing) {
            const float elapsed = std::chrono::duration<float>(now - pip_last_render_time_).count();
            if (elapsed < 1.0f / PREVIEW_TARGET_FPS)
                return;
        }

        auto* const rm = ctx.viewer->getRenderingManager();
        auto* const sm = ctx.viewer->getSceneManager();
        if (!rm || !sm)
            return;

        if (!pip_initialized_)
            initPipPreview();

        glm::mat3 cam_rot;
        glm::vec3 cam_pos;
        float cam_fov;

        if (is_playing) {
            const auto state = sequencer_controller_.currentCameraState();
            cam_rot = glm::mat3_cast(state.rotation);
            cam_pos = state.position;
            cam_fov = state.fov;
        } else {
            if (pip_last_keyframe_ == selected && !pip_needs_update_)
                return;

            const auto& timeline = sequencer_controller_.timeline();
            if (*selected >= timeline.size())
                return;

            const auto* const kf = timeline.getKeyframe(*selected);
            if (!kf)
                return;

            cam_rot = glm::mat3_cast(kf->rotation);
            cam_pos = kf->position;
            cam_fov = kf->fov;
        }

        if (rm->renderPreviewFrame(sm, cam_rot, cam_pos, cam_fov, pip_fbo_, pip_texture_, PREVIEW_WIDTH, PREVIEW_HEIGHT)) {
            pip_last_render_time_ = now;
            if (!is_playing) {
                pip_last_keyframe_ = selected;
                pip_needs_update_ = false;
            }
        }
    }

    void GuiManager::drawPipPreviewWindow([[maybe_unused]] const UIContext& ctx) {
        const bool is_playing = !sequencer_controller_.isStopped();
        const auto selected = sequencer_controller_.selectedKeyframe();

        if (!is_playing && !selected.has_value())
            return;
        if (!pip_initialized_ || pip_texture_ == 0)
            return;

        if (!is_playing) {
            const auto& timeline = sequencer_controller_.timeline();
            if (*selected >= timeline.size())
                return;
            const auto* const kf = timeline.getKeyframe(*selected);
            if (!kf || kf->is_loop_point)
                return;
        }

        const auto& t = theme();
        const float scale = sequencer_ui_state_.pip_preview_scale;
        constexpr float MARGIN = 16.0f;
        constexpr float PANEL_HEIGHT = 90.0f;
        const float scaled_width = static_cast<float>(PREVIEW_WIDTH) * scale;
        const float scaled_height = static_cast<float>(PREVIEW_HEIGHT) * scale;
        const ImVec2 window_size(scaled_width, scaled_height + 24.0f);
        const ImVec2 window_pos(
            viewport_pos_.x + MARGIN,
            viewport_pos_.y + viewport_size_.y - PANEL_HEIGHT - window_size.y - MARGIN);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);

        // Use different border color during playback
        const ImU32 border_color = is_playing
                                       ? t.error_u32()
                                       : toU32WithAlpha(t.palette.primary, 0.6f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, toU32(t.palette.surface));
        ImGui::PushStyleColor(ImGuiCol_Border, border_color);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, t.sizes.window_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.0f, 4.0f});

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;

        if (ImGui::Begin("##KeyframePreview", nullptr, flags)) {
            const std::string title = is_playing
                                          ? std::format("Playback {:.2f}s", sequencer_controller_.playhead())
                                          : std::format("Keyframe {} Preview", *selected + 1);
            ImGui::TextColored({t.palette.text.x, t.palette.text.y, t.palette.text.z, 0.8f}, "%s", title.c_str());
            ImGui::Image(static_cast<ImTextureID>(pip_texture_),
                         {scaled_width - 8.0f, scaled_height - 8.0f}, {0, 1}, {1, 0});
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void GuiManager::renderDockedPythonConsole(const UIContext& ctx, float panel_x, float panel_h) {
        const auto* const vp = ImGui::GetMainViewport();
        const auto& io = ImGui::GetIO();
        constexpr float EDGE_GRAB_W = 8.0f;

        // Check if hovering over left edge for resize
        python_console_hovering_edge_ = io.MousePos.x >= panel_x - EDGE_GRAB_W &&
                                        io.MousePos.x <= panel_x + EDGE_GRAB_W &&
                                        io.MousePos.y >= vp->WorkPos.y &&
                                        io.MousePos.y <= vp->WorkPos.y + panel_h;

        if (python_console_hovering_edge_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            python_console_resizing_ = true;
        if (python_console_resizing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            python_console_resizing_ = false;
        if (python_console_resizing_) {
            const float max_console_w = vp->WorkSize.x * PYTHON_CONSOLE_MAX_RATIO;
            python_console_width_ = std::clamp(python_console_width_ - io.MouseDelta.x,
                                               PYTHON_CONSOLE_MIN_WIDTH, max_console_w);
            updateViewportRegion();
        }
        if (python_console_hovering_edge_ || python_console_resizing_)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        // Draw the docked console panel
        const ImVec2 pos{panel_x, vp->WorkPos.y};
        const ImVec2 size{python_console_width_, panel_h};
        panels::DrawDockedPythonConsole(ctx, pos, size);
    }

    void GuiManager::renderPythonPanels([[maybe_unused]] const UIContext& ctx) {
        lfs::vis::Scene* scene = nullptr;
        if (auto* sm = ctx.viewer->getSceneManager()) {
            scene = &sm->getScene();
        }
        python::draw_python_panels(python::PanelSpace::Floating, scene);
        python::draw_python_panels(python::PanelSpace::ViewportOverlay, scene);
        python::draw_python_modals(scene);
        python::draw_python_popups(scene);
    }

    void GuiManager::triggerCropFlash() {
        crop_flash_active_ = true;
        crop_flash_start_ = std::chrono::steady_clock::now();
    }

    void GuiManager::updateCropFlash() {
        if (!crop_flash_active_)
            return;

        auto* const sm = viewer_->getSceneManager();
        auto* const rm = viewer_->getRenderingManager();
        if (!sm || !rm)
            return;

        constexpr int DURATION_MS = 400;
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - crop_flash_start_)
                                    .count();

        const NodeId cropbox_id = sm->getSelectedNodeCropBoxId();
        if (cropbox_id == NULL_NODE) {
            crop_flash_active_ = false;
            return;
        }

        auto* node = sm->getScene().getMutableNode(sm->getScene().getNodeById(cropbox_id)->name);
        if (!node || !node->cropbox) {
            crop_flash_active_ = false;
            return;
        }

        if (elapsed_ms >= DURATION_MS) {
            crop_flash_active_ = false;
            node->cropbox->flash_intensity = 0.0f;
        } else {
            node->cropbox->flash_intensity = 1.0f - static_cast<float>(elapsed_ms) / DURATION_MS;
        }
        sm->getScene().invalidateCache();
        rm->markDirty();
    }

    void GuiManager::deactivateAllTools() {
        // Selection tool is handled by Python operator - cancel it
        python::cancel_active_operator();
        if (auto* const t = viewer_->getBrushTool())
            t->setEnabled(false);
        if (auto* const t = viewer_->getAlignTool())
            t->setEnabled(false);

        if (auto* const sm = viewer_->getSceneManager()) {
            sm->applyDeleted();
        }

        auto& editor = viewer_->getEditorContext();
        editor.setActiveTool(ToolType::None);
        current_operation_ = ImGuizmo::TRANSLATE;
    }

    void GuiManager::setupEventHandlers() {
        using namespace lfs::core::events;

        ui::FileDropReceived::when([this](const auto&) {
            show_startup_overlay_ = false;
        });

        cmd::ShowWindow::when([this](const auto& e) {
            showWindow(e.window_name, e.show);
        });

        ui::NodeSelected::when([this](const auto&) {
            // Selection tool is handled by Python operator - cancel it
            python::cancel_active_operator();
            if (auto* const t = viewer_->getBrushTool())
                t->setEnabled(false);
            if (auto* const t = viewer_->getAlignTool())
                t->setEnabled(false);
            if (auto* const sm = viewer_->getSceneManager())
                sm->syncCropBoxToRenderSettings();
        });
        ui::NodeDeselected::when([this](const auto&) {
            // Selection tool is handled by Python operator - cancel it
            python::cancel_active_operator();
            if (auto* const t = viewer_->getBrushTool())
                t->setEnabled(false);
            if (auto* const t = viewer_->getAlignTool())
                t->setEnabled(false);
        });
        state::PLYRemoved::when([this](const auto&) { deactivateAllTools(); });
        state::SceneCleared::when([this](const auto&) { deactivateAllTools(); });

        cmd::GoToCamView::when([this](const auto& e) {
            if (auto* sm = viewer_->getSceneManager()) {
                const auto& scene = sm->getScene();
                for (const auto* node : scene.getNodes()) {
                    if (node->type == NodeType::CAMERA && node->camera_uid == e.cam_id) {
                        ui::NodeSelected{.path = node->name, .type = "Camera", .metadata = {}}.emit();
                        break;
                    }
                }
            }
        });

        lfs::core::events::tools::SetToolbarTool::when([this](const auto& e) {
            auto& editor = viewer_->getEditorContext();
            const auto tool = static_cast<ToolType>(e.tool_mode);

            if (editor.hasActiveOperator() && tool != ToolType::Selection) {
                python::cancel_active_operator();
            }

            editor.setActiveTool(tool);

            static constexpr std::array<const char*, 8> TOOL_IDS = {
                nullptr, "builtin.select", "builtin.translate", "builtin.rotate",
                "builtin.scale", "builtin.brush", "builtin.align", "builtin.mirror"};
            auto& registry = UnifiedToolRegistry::instance();
            const auto idx = static_cast<size_t>(tool);
            if (idx < TOOL_IDS.size() && TOOL_IDS[idx]) {
                registry.setActiveTool(TOOL_IDS[idx]);
            } else {
                registry.clearActiveTool();
            }

            switch (tool) {
            case ToolType::Translate:
                current_operation_ = ImGuizmo::TRANSLATE;
                LOG_INFO("SetToolbarTool: TRANSLATE");
                break;
            case ToolType::Rotate:
                current_operation_ = ImGuizmo::ROTATE;
                LOG_INFO("SetToolbarTool: ROTATE");
                break;
            case ToolType::Scale:
                current_operation_ = ImGuizmo::SCALE;
                LOG_INFO("SetToolbarTool: SCALE");
                break;
            case ToolType::Selection:
                // Selection tool is handled natively in C++ via SelectionTool
                break;
            default:
                LOG_INFO("SetToolbarTool: tool_mode={}", e.tool_mode);
                break;
            }
            show_sequencer_ = false;
        });

        lfs::core::events::tools::SetSelectionSubMode::when([this](const auto& e) {
            setSelectionSubMode(static_cast<SelectionSubMode>(e.selection_mode));

            static constexpr std::array<const char*, 5> SUBMODE_IDS = {
                "centers", "rectangle", "polygon", "lasso", "rings"};
            const auto idx = static_cast<size_t>(e.selection_mode);
            if (idx < SUBMODE_IDS.size()) {
                UnifiedToolRegistry::instance().setActiveSubmode(SUBMODE_IDS[idx]);
            }

            if (auto* const tool = viewer_->getSelectionTool()) {
                tool->onSelectionModeChanged();
            }
        });

        lfs::core::events::tools::ExecuteMirror::when([this](const auto& e) {
            auto* sm = viewer_->getSceneManager();
            if (sm) {
                sm->executeMirror(static_cast<lfs::core::MirrorAxis>(e.axis));
            }
        });

        lfs::core::events::tools::CancelActiveOperator::when([](const auto&) {
            lfs::python::cancel_active_operator();
        });

        cmd::ApplyCropBox::when([this](const auto&) {
            auto* const sm = viewer_->getSceneManager();
            if (!sm)
                return;

            // Check if a cropbox node is selected
            const NodeId cropbox_id = sm->getSelectedNodeCropBoxId();
            if (cropbox_id == NULL_NODE)
                return;

            const auto* cropbox_node = sm->getScene().getNodeById(cropbox_id);
            if (!cropbox_node || !cropbox_node->cropbox)
                return;

            const glm::mat4 world_transform = sm->getScene().getWorldTransform(cropbox_id);

            lfs::geometry::BoundingBox crop_box;
            crop_box.setBounds(cropbox_node->cropbox->min, cropbox_node->cropbox->max);
            crop_box.setworld2BBox(glm::inverse(world_transform));
            cmd::CropPLY{.crop_box = crop_box, .inverse = cropbox_node->cropbox->inverse}.emit();
            triggerCropFlash();
        });

        cmd::ApplyEllipsoid::when([this](const auto&) {
            auto* const sm = viewer_->getSceneManager();
            if (!sm)
                return;

            const NodeId ellipsoid_id = sm->getSelectedNodeEllipsoidId();
            if (ellipsoid_id == NULL_NODE)
                return;

            const auto* ellipsoid_node = sm->getScene().getNodeById(ellipsoid_id);
            if (!ellipsoid_node || !ellipsoid_node->ellipsoid)
                return;

            const glm::mat4 world_transform = sm->getScene().getWorldTransform(ellipsoid_id);
            const glm::vec3 radii = ellipsoid_node->ellipsoid->radii;
            const bool inverse = ellipsoid_node->ellipsoid->inverse;

            cmd::CropPLYEllipsoid{
                .world_transform = world_transform,
                .radii = radii,
                .inverse = inverse}
                .emit();
            triggerCropFlash();
        });

        // Handle Ctrl+T to toggle crop inverse mode
        cmd::ToggleCropInverse::when([this](const auto&) {
            auto* const sm = viewer_->getSceneManager();
            if (!sm)
                return;

            // Check if a cropbox node is selected
            const NodeId cropbox_id = sm->getSelectedNodeCropBoxId();
            if (cropbox_id == NULL_NODE)
                return;

            auto* node = sm->getScene().getMutableNode(sm->getScene().getNodeById(cropbox_id)->name);
            if (!node || !node->cropbox)
                return;

            node->cropbox->inverse = !node->cropbox->inverse;
            sm->getScene().invalidateCache();
        });

        // Cycle: normal -> center markers -> rings -> normal
        cmd::CycleSelectionVisualization::when([this](const auto&) {
            if (viewer_->getEditorContext().getActiveTool() != ToolType::Selection)
                return;
            auto* const rm = viewer_->getRenderingManager();
            if (!rm)
                return;

            auto settings = rm->getSettings();
            const bool centers = settings.show_center_markers;
            const bool rings = settings.show_rings;

            settings.show_center_markers = !centers && !rings;
            settings.show_rings = centers && !rings;
            rm->updateSettings(settings);
        });

        ui::FocusTrainingPanel::when([this](const auto&) {
            focus_panel_name_ = "Training";
        });

        ui::ToggleUI::when([this](const auto&) {
            ui_hidden_ = !ui_hidden_;
        });

        ui::ToggleFullscreen::when([this](const auto&) {
            if (auto* wm = viewer_->getWindowManager()) {
                wm->toggleFullscreen();
            }
        });

        cmd::SequencerAddKeyframe::when([this](const auto&) {
            const auto& cam = viewer_->getViewport().camera;
            auto& timeline = sequencer_controller_.timeline();

            // Use snap interval if enabled, otherwise default to 1.0f
            const float interval = sequencer_ui_state_.snap_to_grid
                                       ? sequencer_ui_state_.snap_interval
                                       : 1.0f;
            const float time = timeline.empty() ? 0.0f : timeline.endTime() + interval;

            lfs::sequencer::Keyframe kf;
            kf.time = time;
            kf.position = cam.t;
            kf.rotation = glm::quat_cast(cam.R);
            kf.fov = lfs::sequencer::DEFAULT_FOV;
            timeline.addKeyframe(kf);
            sequencer_controller_.seek(time);
        });

        cmd::SequencerUpdateKeyframe::when([this](const auto&) {
            if (!sequencer_controller_.hasSelection())
                return;
            const auto& cam = viewer_->getViewport().camera;
            sequencer_controller_.updateSelectedKeyframe(
                cam.t,
                glm::quat_cast(cam.R),
                lfs::sequencer::DEFAULT_FOV);
        });

        cmd::SequencerPlayPause::when([this](const auto&) {
            sequencer_controller_.togglePlayPause();
        });

        cmd::SequencerExportVideo::when([this](const auto& evt) {
            const auto path = SaveMp4FileDialog("camera_path");
            if (path.empty())
                return;

            io::video::VideoExportOptions options;
            options.width = evt.width;
            options.height = evt.height;
            options.framerate = evt.framerate;
            options.crf = evt.crf;
            startVideoExport(path, options);
        });

        state::DiskSpaceSaveFailed::when([this](const auto& e) {
            // Non-disk-space errors are handled by notification_bridge.cpp
            if (!e.is_disk_space_error)
                return;

            if (!disk_space_error_dialog_)
                return;

            const DiskSpaceErrorDialog::ErrorInfo info{
                .path = e.path,
                .error_message = e.error,
                .required_bytes = e.required_bytes,
                .available_bytes = e.available_bytes,
                .iteration = e.iteration,
                .is_checkpoint = e.is_checkpoint};

            if (e.is_checkpoint) {
                auto on_retry = [this, iteration = e.iteration]() {
                    if (auto* tm = viewer_->getTrainerManager()) {
                        if (tm->isFinished() || !tm->isTrainingActive()) {
                            if (auto* trainer = tm->getTrainer()) {
                                LOG_INFO("Retrying save at iteration {}", iteration);
                                trainer->save_final_ply_and_checkpoint(iteration);
                            }
                        } else {
                            tm->requestSaveCheckpoint();
                        }
                    }
                };

                auto on_change_location = [this, iteration = e.iteration](const std::filesystem::path& new_path) {
                    if (auto* tm = viewer_->getTrainerManager()) {
                        if (auto* trainer = tm->getTrainer()) {
                            auto params = trainer->getParams();
                            params.dataset.output_path = new_path;
                            trainer->setParams(params);
                            LOG_INFO("Output path changed to: {}", lfs::core::path_to_utf8(new_path));

                            if (tm->isFinished() || !tm->isTrainingActive()) {
                                trainer->save_final_ply_and_checkpoint(iteration);
                            } else {
                                tm->requestSaveCheckpoint();
                            }
                        }
                    }
                };

                auto on_cancel = []() {
                    LOG_WARN("Checkpoint save cancelled by user");
                };

                disk_space_error_dialog_->show(info, on_retry, on_change_location, on_cancel);
            } else {
                auto on_retry = []() {};

                auto on_change_location = [](const std::filesystem::path& new_path) {
                    LOG_INFO("Re-export manually using File > Export to: {}", lfs::core::path_to_utf8(new_path));
                };

                auto on_cancel = []() {
                    LOG_INFO("Export cancelled by user");
                };

                disk_space_error_dialog_->show(info, on_retry, on_change_location, on_cancel);
            }
        });

        cmd::LoadFile::when([this](const auto& cmd) {
            if (!cmd.is_dataset) {
                return;
            }
            const auto* const data_loader = viewer_->getDataLoader();
            if (!data_loader) {
                LOG_ERROR("LoadFile: no data loader");
                return;
            }
            auto params = data_loader->getParameters();
            if (!cmd.output_path.empty()) {
                params.dataset.output_path = cmd.output_path;
            }
            if (!cmd.init_path.empty()) {
                params.init_path = lfs::core::path_to_utf8(cmd.init_path);
            }
            startAsyncImport(cmd.path, params);
        });

        // Fallback sync import progress handlers
        state::DatasetLoadStarted::when([this](const auto& e) {
            if (import_state_.active.load()) {
                return;
            }
            const std::lock_guard lock(import_state_.mutex);
            import_state_.active.store(true);
            import_state_.progress.store(0.0f);
            import_state_.path = e.path;
            import_state_.stage = "Initializing...";
            import_state_.error.clear();
            import_state_.num_images = 0;
            import_state_.num_points = 0;
            import_state_.success = false;
            import_state_.dataset_type = getDatasetTypeName(e.path);
        });

        state::DatasetLoadProgress::when([this](const auto& e) {
            import_state_.progress.store(e.progress / 100.0f);
            const std::lock_guard lock(import_state_.mutex);
            import_state_.stage = e.step;
        });

        state::DatasetLoadCompleted::when([this](const auto& e) {
            if (import_state_.show_completion.load()) {
                return;
            }
            {
                const std::lock_guard lock(import_state_.mutex);
                import_state_.success = e.success;
                import_state_.num_images = e.num_images;
                import_state_.num_points = e.num_points;
                import_state_.completion_time = std::chrono::steady_clock::now();
                import_state_.error = e.error.value_or("");
                import_state_.stage = e.success ? "Complete" : "Failed";
                import_state_.progress.store(1.0f);
            }
            import_state_.active.store(false);
            import_state_.show_completion.store(true);

            // Focus training panel on successful dataset load
            if (e.success) {
                focus_panel_name_ = "Training";
            }
        });

        internal::TrainerReady::when([this](const auto&) {
            focus_panel_name_ = "Training";
        });
    }

    void GuiManager::setSelectionSubMode(SelectionSubMode mode) {
        selection_mode_ = mode;

        // Also update RenderingManager immediately so Python get_active_submode() returns correct value
        if (auto* rm = viewer_->getRenderingManager()) {
            lfs::rendering::SelectionMode rm_mode = lfs::rendering::SelectionMode::Centers;
            switch (mode) {
            case SelectionSubMode::Centers: rm_mode = lfs::rendering::SelectionMode::Centers; break;
            case SelectionSubMode::Rectangle: rm_mode = lfs::rendering::SelectionMode::Rectangle; break;
            case SelectionSubMode::Polygon: rm_mode = lfs::rendering::SelectionMode::Polygon; break;
            case SelectionSubMode::Lasso: rm_mode = lfs::rendering::SelectionMode::Lasso; break;
            case SelectionSubMode::Rings: rm_mode = lfs::rendering::SelectionMode::Rings; break;
            }
            rm->setSelectionMode(rm_mode);
        }
    }

    ToolType GuiManager::getCurrentToolMode() const {
        return viewer_->getEditorContext().getActiveTool();
    }

    bool GuiManager::isCapturingInput() const {
        if (auto* input_controller = viewer_->getInputController()) {
            return input_controller->getBindings().isCapturing();
        }
        return false;
    }

    bool GuiManager::isModalWindowOpen() const {
        // Check any ImGui popup/modal (covers Python popups and floating panels)
        return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    }

    void GuiManager::captureKey(int key, int mods) {
        if (auto* input_controller = viewer_->getInputController()) {
            input_controller->getBindings().captureKey(key, mods);
        }
    }

    void GuiManager::captureMouseButton(int button, int mods) {
        if (auto* input_controller = viewer_->getInputController()) {
            input_controller->getBindings().captureMouseButton(button, mods);
        }
    }

    void GuiManager::requestThumbnail(const std::string& video_id) {
        if (menu_bar_) {
            menu_bar_->requestThumbnail(video_id);
        }
    }

    void GuiManager::processThumbnails() {
        if (menu_bar_) {
            menu_bar_->processThumbnails();
        }
    }

    bool GuiManager::isThumbnailReady(const std::string& video_id) const {
        return menu_bar_ ? menu_bar_->isThumbnailReady(video_id) : false;
    }

    uint64_t GuiManager::getThumbnailTexture(const std::string& video_id) const {
        return menu_bar_ ? menu_bar_->getThumbnailTexture(video_id) : 0;
    }

    int GuiManager::getHighlightedCameraUid() const {
        if (auto* sm = viewer_->getSceneManager()) {
            return sm->getSelectedCameraUid();
        }
        return -1;
    }

    void GuiManager::applyDefaultStyle() {
        // Initialize theme system using saved preference
        const bool is_dark = loadThemePreference();
        setTheme(is_dark ? darkTheme() : lightTheme());
    }

    void GuiManager::showWindow(const std::string& name, bool show) {
        window_states_[name] = show;
    }

    void GuiManager::toggleWindow(const std::string& name) {
        window_states_[name] = !window_states_[name];
    }

    bool GuiManager::wantsInput() const {
        // Block all input while exporting
        if (export_state_.active.load() || video_export_state_.active.load()) {
            return true;
        }
        ImGuiIO& io = ImGui::GetIO();
        return io.WantCaptureMouse || io.WantCaptureKeyboard;
    }

    bool GuiManager::isAnyWindowActive() const {
        // Block all interaction while exporting
        if (export_state_.active.load()) {
            return true;
        }
        return ImGui::IsAnyItemActive() ||
               ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ||
               ImGui::GetIO().WantCaptureMouse ||
               ImGui::GetIO().WantCaptureKeyboard;
    }

    bool GuiManager::needsAnimationFrame() const {
        if (video_extractor_dialog_ && video_extractor_dialog_->isVideoPlaying()) {
            return true;
        }
        return false;
    }

    void GuiManager::setFileSelectedCallback(std::function<void(const std::filesystem::path&, bool)> callback) {
        if (file_browser_) {
            file_browser_->setOnFileSelected(callback);
        }
    }

    void GuiManager::renderCropBoxGizmo(const UIContext& ctx) {
        auto* const render_manager = ctx.viewer->getRenderingManager();
        auto* const scene_manager = ctx.viewer->getSceneManager();
        if (!render_manager || !scene_manager)
            return;

        const auto& settings = render_manager->getSettings();
        if (!settings.show_crop_box)
            return;

        NodeId cropbox_id = NULL_NODE;
        const SceneNode* cropbox_node = nullptr;

        if (scene_manager->getSelectedNodeType() == NodeType::CROPBOX) {
            cropbox_id = scene_manager->getSelectedNodeCropBoxId();
        }

        if (cropbox_id == NULL_NODE)
            return;

        cropbox_node = scene_manager->getScene().getNodeById(cropbox_id);
        if (!cropbox_node || !cropbox_node->visible || !cropbox_node->cropbox)
            return;
        if (!scene_manager->getScene().isNodeEffectivelyVisible(cropbox_id))
            return;

        // Camera setup
        auto& viewport = ctx.viewer->getViewport();
        const glm::mat4 view = viewport.getViewMatrix();
        const glm::ivec2 vp_size(static_cast<int>(viewport_size_.x), static_cast<int>(viewport_size_.y));
        const glm::mat4 projection = lfs::rendering::createProjectionMatrixFromFocal(
            vp_size, settings.focal_length_mm, settings.orthographic, settings.ortho_scale);

        // Get cropbox state from scene graph
        const glm::vec3 cropbox_min = cropbox_node->cropbox->min;
        const glm::vec3 cropbox_max = cropbox_node->cropbox->max;
        const glm::mat4 world_transform = scene_manager->getScene().getWorldTransform(cropbox_id);

        // Decompose world transform
        const glm::vec3 local_size = cropbox_max - cropbox_min;
        const glm::vec3 world_scale = gizmo_ops::extractScale(world_transform);
        const glm::mat3 rotation = gizmo_ops::extractRotation(world_transform);
        const glm::vec3 translation = gizmo_ops::extractTranslation(world_transform);

        // Settings
        const bool use_world_space = (transform_space_ == TransformSpace::World);
        const ImGuizmo::OPERATION gizmo_op = current_operation_;

        // Compute pivot based on pivot mode
        const glm::vec3 local_pivot = gizmo_ops::computeLocalPivot(
            scene_manager->getScene(), cropbox_id,
            pivot_mode_, GizmoTargetType::CropBox);
        const glm::vec3 pivot_world = translation + rotation * (local_pivot * world_scale);

        // Build gizmo matrix from frozen context during manipulation, live state otherwise
        const bool gizmo_local_aligned = (gizmo_op == ImGuizmo::SCALE) || !use_world_space;
        glm::mat4 gizmo_matrix;
        if (cropbox_gizmo_active_ && gizmo_context_.isActive()) {
            const auto& target = gizmo_context_.targets[0];
            const glm::vec3 original_size = target.bounds_max - target.bounds_min;
            const glm::vec3 current_size = original_size * gizmo_context_.cumulative_scale;
            const glm::mat3 current_rotation = gizmo_context_.cumulative_rotation * target.rotation;
            const glm::vec3 current_pivot = gizmo_context_.pivot_world + gizmo_context_.cumulative_translation;

            gizmo_matrix = gizmo_ops::computeGizmoMatrix(
                current_pivot, current_rotation, current_size * world_scale,
                gizmo_context_.use_world_space, gizmo_op == ImGuizmo::SCALE);
        } else {
            const glm::vec3 scaled_size = local_size * world_scale;
            gizmo_matrix = glm::translate(glm::mat4(1.0f), pivot_world);
            if (gizmo_local_aligned) {
                gizmo_matrix = gizmo_matrix * glm::mat4(rotation);
            }
            gizmo_matrix = glm::scale(gizmo_matrix, scaled_size);
        }

        // ImGuizmo setup
        ImGuizmo::SetOrthographic(settings.orthographic);
        ImGuizmo::SetRect(viewport_pos_.x, viewport_pos_.y, viewport_size_.x, viewport_size_.y);
        ImGuizmo::SetAxisLimit(GIZMO_AXIS_LIMIT);
        ImGuizmo::SetPlaneLimit(GIZMO_AXIS_LIMIT);

        // Use BOUNDS mode for resize handles when Scale is active
        static const float local_bounds[6] = {-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
        const bool use_bounds = (gizmo_op == ImGuizmo::SCALE);
        const ImGuizmo::OPERATION effective_op = use_bounds ? ImGuizmo::BOUNDS : gizmo_op;
        const float* bounds_ptr = use_bounds ? local_bounds : nullptr;

        {
            static bool s_hovered_axis = false;
            const bool is_using = ImGuizmo::IsUsing();
            if (!is_using) {
                s_hovered_axis = ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X) ||
                                 ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Y) ||
                                 ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Z) ||
                                 ImGuizmo::IsOver(ImGuizmo::BOUNDS);
                ImGuizmo::SetAxisMask(false, false, false);
            } else {
                ImGuizmo::SetAxisMask(s_hovered_axis, s_hovered_axis, s_hovered_axis);
            }
        }

        // Clip to viewport
        ImDrawList* overlay_drawlist = isModalWindowOpen() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
        const ImVec2 clip_min(viewport_pos_.x, viewport_pos_.y);
        const ImVec2 clip_max(clip_min.x + viewport_size_.x, clip_min.y + viewport_size_.y);
        overlay_drawlist->PushClipRect(clip_min, clip_max, true);
        ImGuizmo::SetDrawlist(overlay_drawlist);

        glm::mat4 delta_matrix;
        const ImGuizmo::MODE gizmo_mode = gizmo_local_aligned ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        const bool gizmo_changed = ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            effective_op, gizmo_mode, glm::value_ptr(gizmo_matrix),
            glm::value_ptr(delta_matrix), nullptr, bounds_ptr);

        const bool is_using = ImGuizmo::IsUsing();

        // Capture state when manipulation starts - freeze context for entire drag
        if (is_using && !cropbox_gizmo_active_) {
            cropbox_gizmo_active_ = true;
            cropbox_node_name_ = cropbox_node->name;

            // Capture gizmo context with frozen pivot and original state
            gizmo_context_ = gizmo_ops::captureCropBox(
                scene_manager->getScene(),
                cropbox_node->name,
                pivot_world,
                local_pivot,
                transform_space_,
                pivot_mode_,
                gizmo_op);
        }

        if (gizmo_changed && gizmo_context_.isActive()) {
            auto& scene = scene_manager->getScene();

            if (gizmo_op == ImGuizmo::ROTATE) {
                const glm::mat3 delta_rot = gizmo_ops::extractRotation(delta_matrix);
                gizmo_ops::applyRotation(gizmo_context_, scene, delta_rot);
            } else if (gizmo_op == ImGuizmo::SCALE) {
                // Extract new size from gizmo matrix
                float mat_trans[3], mat_rot[3], mat_scale[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmo_matrix), mat_trans, mat_rot, mat_scale);
                const glm::vec3 new_size = glm::max(
                    glm::vec3(mat_scale[0], mat_scale[1], mat_scale[2]) / world_scale,
                    glm::vec3(MIN_GIZMO_SCALE));
                gizmo_ops::applyBoundsScale(gizmo_context_, scene, new_size);

                // Update position to match gizmo center
                const glm::vec3 new_pivot_world(mat_trans[0], mat_trans[1], mat_trans[2]);
                gizmo_ops::applyTranslation(gizmo_context_, scene, new_pivot_world);
            } else {
                // TRANSLATE
                const glm::vec3 new_pivot_world(gizmo_matrix[3]);
                gizmo_ops::applyTranslation(gizmo_context_, scene, new_pivot_world);
            }

            render_manager->markDirty();
        }

        if (!is_using && cropbox_gizmo_active_) {
            cropbox_gizmo_active_ = false;
            gizmo_context_.reset();

            auto* node = scene_manager->getScene().getMutableNode(cropbox_node_name_);
            if (node && node->cropbox) {
                using namespace lfs::core::events;
                ui::CropBoxChanged{
                    .min_bounds = node->cropbox->min,
                    .max_bounds = node->cropbox->max,
                    .enabled = settings.use_crop_box}
                    .emit();
            }
        }

        // Sync state for wireframe rendering
        if (cropbox_gizmo_active_) {
            render_manager->setCropboxGizmoState(
                true, cropbox_node->cropbox->min, cropbox_node->cropbox->max,
                scene_manager->getScene().getWorldTransform(cropbox_id));
        } else {
            render_manager->setCropboxGizmoActive(false);
        }

        overlay_drawlist->PopClipRect();
    }

    void GuiManager::renderCropGizmoMiniToolbar(const UIContext&) {
        constexpr float MARGIN_X = 10.0f;
        constexpr float MARGIN_BOTTOM = 100.0f;
        constexpr int BUTTON_COUNT = 3;
        constexpr ImGuiWindowFlags WINDOW_FLAGS =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        const auto& t = theme();
        const float scale = lfs::python::get_shared_dpi_scale();
        const float btn_size = t.sizes.toolbar_button_size * scale;
        const float spacing = t.sizes.toolbar_spacing * scale;
        const float padding = t.sizes.toolbar_padding * scale;
        const float toolbar_width = BUTTON_COUNT * btn_size + (BUTTON_COUNT - 1) * spacing + 2.0f * padding;
        const float toolbar_height = btn_size + 2.0f * padding;
        const float toolbar_x = viewport_pos_.x + MARGIN_X * scale;
        const float toolbar_y = viewport_pos_.y + viewport_size_.y - MARGIN_BOTTOM * scale;

        widgets::DrawWindowShadow({toolbar_x, toolbar_y}, {toolbar_width, toolbar_height}, t.sizes.window_rounding);
        ImGui::SetNextWindowPos({toolbar_x, toolbar_y});
        ImGui::SetNextWindowSize({toolbar_width, toolbar_height});

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, t.sizes.window_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, t.subtoolbar_background());

        if (ImGui::Begin("##CropGizmoMiniToolbar", nullptr, WINDOW_FLAGS)) {
            const ImVec2 btn_sz(btn_size, btn_size);

            const auto button = [&](const char* id, const ImGuizmo::OPERATION op, const char* label, const char* tip) {
                if (widgets::IconButton(id, 0, btn_sz, current_operation_ == op, label)) {
                    current_operation_ = op;
                }
                if (ImGui::IsItemHovered()) {
                    widgets::SetThemedTooltip("%s", tip);
                }
            };

            button("##mini_t", ImGuizmo::TRANSLATE, "T", "Translate (T)");
            ImGui::SameLine();
            button("##mini_r", ImGuizmo::ROTATE, "R", "Rotate (R)");
            ImGui::SameLine();
            button("##mini_s", ImGuizmo::SCALE, "S", "Scale (S)");
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
    }

    void GuiManager::renderEllipsoidGizmo(const UIContext& ctx) {
        auto* const render_manager = ctx.viewer->getRenderingManager();
        auto* const scene_manager = ctx.viewer->getSceneManager();
        if (!render_manager || !scene_manager)
            return;

        const auto& settings = render_manager->getSettings();
        if (!settings.show_ellipsoid)
            return;

        NodeId ellipsoid_id = NULL_NODE;
        const SceneNode* ellipsoid_node = nullptr;

        if (scene_manager->getSelectedNodeType() == NodeType::ELLIPSOID) {
            ellipsoid_id = scene_manager->getSelectedNodeEllipsoidId();
        }

        if (ellipsoid_id == NULL_NODE)
            return;

        ellipsoid_node = scene_manager->getScene().getNodeById(ellipsoid_id);
        if (!ellipsoid_node || !ellipsoid_node->visible || !ellipsoid_node->ellipsoid)
            return;
        if (!scene_manager->getScene().isNodeEffectivelyVisible(ellipsoid_id))
            return;

        // Camera setup
        auto& viewport = ctx.viewer->getViewport();
        const glm::mat4 view = viewport.getViewMatrix();
        const glm::ivec2 vp_size(static_cast<int>(viewport_size_.x), static_cast<int>(viewport_size_.y));
        const glm::mat4 projection = lfs::rendering::createProjectionMatrixFromFocal(
            vp_size, settings.focal_length_mm, settings.orthographic, settings.ortho_scale);

        const glm::vec3 radii = ellipsoid_node->ellipsoid->radii;
        const glm::mat4 world_transform = scene_manager->getScene().getWorldTransform(ellipsoid_id);

        // Decompose world transform
        const glm::vec3 world_scale = gizmo_ops::extractScale(world_transform);
        const glm::mat3 rotation = gizmo_ops::extractRotation(world_transform);
        const glm::vec3 translation = gizmo_ops::extractTranslation(world_transform);

        // Settings
        const bool use_world_space = (transform_space_ == TransformSpace::World);
        const ImGuizmo::OPERATION gizmo_op = current_operation_;

        // Ellipsoid pivot is always at center (origin in local space)
        const glm::vec3 local_pivot(0.0f);
        const glm::vec3 pivot_world = translation;

        // Build gizmo matrix from frozen context during manipulation, live state otherwise
        const bool gizmo_local_aligned = (gizmo_op == ImGuizmo::SCALE) || !use_world_space;
        glm::mat4 gizmo_matrix;
        if (ellipsoid_gizmo_active_ && gizmo_context_.isActive()) {
            const auto& target = gizmo_context_.targets[0];
            const glm::vec3 current_radii = target.radii * gizmo_context_.cumulative_scale;
            const glm::mat3 current_rotation = gizmo_context_.cumulative_rotation * target.rotation;
            const glm::vec3 current_pivot = gizmo_context_.pivot_world + gizmo_context_.cumulative_translation;

            gizmo_matrix = gizmo_ops::computeGizmoMatrix(
                current_pivot, current_rotation, current_radii * world_scale,
                gizmo_context_.use_world_space, gizmo_op == ImGuizmo::SCALE);
        } else {
            const glm::vec3 scaled_radii = radii * world_scale;
            gizmo_matrix = glm::translate(glm::mat4(1.0f), pivot_world);
            if (gizmo_local_aligned) {
                gizmo_matrix = gizmo_matrix * glm::mat4(rotation);
            }
            gizmo_matrix = glm::scale(gizmo_matrix, scaled_radii);
        }

        // ImGuizmo setup
        ImGuizmo::SetOrthographic(settings.orthographic);
        ImGuizmo::SetRect(viewport_pos_.x, viewport_pos_.y, viewport_size_.x, viewport_size_.y);
        ImGuizmo::SetAxisLimit(GIZMO_AXIS_LIMIT);
        ImGuizmo::SetPlaneLimit(GIZMO_AXIS_LIMIT);

        // Use BOUNDS mode for resize handles when Scale is active
        // Ellipsoid uses unit sphere bounds (-1 to 1) since radii are in the scale component
        static const float local_bounds[6] = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        const bool use_bounds = (gizmo_op == ImGuizmo::SCALE);
        const ImGuizmo::OPERATION effective_op = use_bounds ? ImGuizmo::BOUNDS : gizmo_op;
        const float* bounds_ptr = use_bounds ? local_bounds : nullptr;

        {
            static bool s_hovered_axis = false;
            const bool is_using = ImGuizmo::IsUsing();
            if (!is_using) {
                s_hovered_axis = ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X) ||
                                 ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Y) ||
                                 ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Z) ||
                                 ImGuizmo::IsOver(ImGuizmo::BOUNDS);
                ImGuizmo::SetAxisMask(false, false, false);
            } else {
                ImGuizmo::SetAxisMask(s_hovered_axis, s_hovered_axis, s_hovered_axis);
            }
        }

        // Clip to viewport
        ImDrawList* overlay_drawlist = isModalWindowOpen() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
        const ImVec2 clip_min(viewport_pos_.x, viewport_pos_.y);
        const ImVec2 clip_max(clip_min.x + viewport_size_.x, clip_min.y + viewport_size_.y);
        overlay_drawlist->PushClipRect(clip_min, clip_max, true);
        ImGuizmo::SetDrawlist(overlay_drawlist);

        glm::mat4 delta_matrix;
        const ImGuizmo::MODE gizmo_mode = gizmo_local_aligned ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        const bool gizmo_changed = ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            effective_op, gizmo_mode, glm::value_ptr(gizmo_matrix),
            glm::value_ptr(delta_matrix), nullptr, bounds_ptr);

        const bool is_using = ImGuizmo::IsUsing();

        // Capture state when manipulation starts - freeze context for entire drag
        if (is_using && !ellipsoid_gizmo_active_) {
            ellipsoid_gizmo_active_ = true;
            ellipsoid_node_name_ = ellipsoid_node->name;

            // Capture gizmo context with frozen pivot and original state
            gizmo_context_ = gizmo_ops::captureEllipsoid(
                scene_manager->getScene(),
                ellipsoid_node->name,
                pivot_world,
                local_pivot,
                transform_space_,
                pivot_mode_,
                gizmo_op);
        }

        if (gizmo_changed && gizmo_context_.isActive()) {
            auto& scene = scene_manager->getScene();

            if (gizmo_op == ImGuizmo::ROTATE) {
                const glm::mat3 delta_rot = gizmo_ops::extractRotation(delta_matrix);
                gizmo_ops::applyRotation(gizmo_context_, scene, delta_rot);
            } else if (gizmo_op == ImGuizmo::SCALE) {
                // Extract new radii from gizmo matrix
                float mat_trans[3], mat_rot[3], mat_scale[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmo_matrix), mat_trans, mat_rot, mat_scale);
                const glm::vec3 new_radii = glm::max(
                    glm::vec3(mat_scale[0], mat_scale[1], mat_scale[2]) / world_scale,
                    glm::vec3(MIN_GIZMO_SCALE));
                gizmo_ops::applyBoundsScale(gizmo_context_, scene, new_radii);

                // Update position to match gizmo center
                const glm::vec3 new_pivot_world(mat_trans[0], mat_trans[1], mat_trans[2]);
                gizmo_ops::applyTranslation(gizmo_context_, scene, new_pivot_world);
            } else {
                // TRANSLATE
                const glm::vec3 new_pivot_world(gizmo_matrix[3]);
                gizmo_ops::applyTranslation(gizmo_context_, scene, new_pivot_world);
            }

            render_manager->markDirty();
        }

        if (!is_using && ellipsoid_gizmo_active_) {
            ellipsoid_gizmo_active_ = false;
            gizmo_context_.reset();

            auto* node = scene_manager->getScene().getMutableNode(ellipsoid_node_name_);
            if (node && node->ellipsoid) {
                using namespace lfs::core::events;
                ui::EllipsoidChanged{
                    .radii = node->ellipsoid->radii,
                    .enabled = settings.use_ellipsoid}
                    .emit();
            }
        }

        // Sync gizmo state with current manipulation values for wireframe rendering
        if (ellipsoid_gizmo_active_) {
            const glm::mat4 current_world_transform = scene_manager->getScene().getWorldTransform(ellipsoid_id);
            render_manager->setEllipsoidGizmoState(true, ellipsoid_node->ellipsoid->radii,
                                                   current_world_transform);
        } else {
            render_manager->setEllipsoidGizmoActive(false);
        }

        overlay_drawlist->PopClipRect();
    }

    void GuiManager::renderNodeTransformGizmo(const UIContext& ctx) {
        if (!show_node_gizmo_)
            return;

        auto* scene_manager = ctx.viewer->getSceneManager();
        if (!scene_manager || !scene_manager->hasSelectedNode())
            return;

        const auto selected_type = scene_manager->getSelectedNodeType();
        if (selected_type == NodeType::CROPBOX || selected_type == NodeType::ELLIPSOID)
            return;

        // Check visibility of at least one selected node
        const auto& scene = scene_manager->getScene();
        const auto selected_names = scene_manager->getSelectedNodeNames();
        bool any_visible = false;
        for (const auto& name : selected_names) {
            if (const auto* node = scene.getNode(name)) {
                if (scene.isNodeEffectivelyVisible(node->id)) {
                    any_visible = true;
                    break;
                }
            }
        }
        if (!any_visible)
            return;

        auto* render_manager = ctx.viewer->getRenderingManager();
        if (!render_manager)
            return;

        const auto& settings = render_manager->getSettings();
        const bool is_multi_selection = (selected_names.size() > 1);

        // Camera matrices
        auto& viewport = ctx.viewer->getViewport();
        const glm::mat4 view = viewport.getViewMatrix();
        const glm::ivec2 vp_size(static_cast<int>(viewport_size_.x), static_cast<int>(viewport_size_.y));
        const glm::mat4 projection = lfs::rendering::createProjectionMatrixFromFocal(
            vp_size, settings.focal_length_mm, settings.orthographic, settings.ortho_scale);

        const bool use_world_space = (transform_space_ == TransformSpace::World) || is_multi_selection;

        const glm::vec3 local_pivot = (pivot_mode_ == PivotMode::Origin)
                                          ? glm::vec3(0.0f)
                                          : scene_manager->getSelectionCenter();

        const glm::vec3 gizmo_position = node_gizmo_active_
                                             ? gizmo_pivot_
                                             : (is_multi_selection
                                                    ? scene_manager->getSelectionWorldCenter()
                                                    : glm::vec3(scene_manager->getSelectedNodeWorldTransform() *
                                                                glm::vec4(local_pivot, 1.0f)));

        glm::mat4 gizmo_matrix(1.0f);
        gizmo_matrix[3] = glm::vec4(gizmo_position, 1.0f);

        if (!is_multi_selection && !use_world_space) {
            const glm::mat3 rotation_scale(scene_manager->getSelectedNodeWorldTransform());
            gizmo_matrix[0] = glm::vec4(rotation_scale[0], 0.0f);
            gizmo_matrix[1] = glm::vec4(rotation_scale[1], 0.0f);
            gizmo_matrix[2] = glm::vec4(rotation_scale[2], 0.0f);
        }

        ImGuizmo::SetOrthographic(settings.orthographic);
        ImGuizmo::SetRect(viewport_pos_.x, viewport_pos_.y, viewport_size_.x, viewport_size_.y);
        ImGuizmo::SetAxisLimit(GIZMO_AXIS_LIMIT);
        ImGuizmo::SetPlaneLimit(GIZMO_AXIS_LIMIT);

        static bool s_node_hovered_axis = false;
        const bool is_using = ImGuizmo::IsUsing();

        if (!is_using) {
            s_node_hovered_axis = ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X) ||
                                  ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Y) ||
                                  ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Z);
            ImGuizmo::SetAxisMask(false, false, false);
        } else {
            ImGuizmo::SetAxisMask(s_node_hovered_axis, s_node_hovered_axis, s_node_hovered_axis);
        }

        // Use background drawlist when modal is open to render below dialogs
        ImDrawList* overlay_drawlist = isModalWindowOpen() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
        const ImVec2 clip_min(viewport_pos_.x, viewport_pos_.y);
        const ImVec2 clip_max(clip_min.x + viewport_size_.x, clip_min.y + viewport_size_.y);
        overlay_drawlist->PushClipRect(clip_min, clip_max, true);
        ImGuizmo::SetDrawlist(overlay_drawlist);

        const ImGuizmo::MODE gizmo_mode = use_world_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

        glm::mat4 delta_matrix;
        const bool gizmo_changed = ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(projection),
            node_gizmo_operation_, gizmo_mode,
            glm::value_ptr(gizmo_matrix), glm::value_ptr(delta_matrix), nullptr);

        if (node_gizmo_operation_ == ImGuizmo::ROTATE) {
            const glm::vec4 clip_pos = projection * view * glm::vec4(gizmo_position, 1.0f);
            if (clip_pos.w > 0.0f) {
                const glm::vec2 ndc(clip_pos.x / clip_pos.w, clip_pos.y / clip_pos.w);
                const ImVec2 screen_pos(viewport_pos_.x + (ndc.x * 0.5f + 0.5f) * viewport_size_.x,
                                        viewport_pos_.y + (-ndc.y * 0.5f + 0.5f) * viewport_size_.y);
                constexpr float PIVOT_RADIUS = 4.0f;
                constexpr ImU32 PIVOT_COLOR = IM_COL32(255, 255, 255, 200);
                constexpr ImU32 PIVOT_OUTLINE = IM_COL32(0, 0, 0, 200);
                overlay_drawlist->AddCircleFilled(screen_pos, PIVOT_RADIUS + 1.0f, PIVOT_OUTLINE);
                overlay_drawlist->AddCircleFilled(screen_pos, PIVOT_RADIUS, PIVOT_COLOR);
            }
        }

        // Capture state for undo when drag starts
        if (is_using && !node_gizmo_active_) {
            node_gizmo_active_ = true;
            gizmo_pivot_ = gizmo_position;
            gizmo_cumulative_rotation_ = glm::mat3(1.0f);
            gizmo_cumulative_scale_ = glm::vec3(1.0f);

            // Filter out nodes whose ancestors are also selected
            std::unordered_set<NodeId> selected_ids;
            for (const auto& name : selected_names) {
                if (const auto* node = scene.getNode(name)) {
                    selected_ids.insert(node->id);
                }
            }

            node_gizmo_node_names_.clear();
            for (const auto& name : selected_names) {
                const auto* node = scene.getNode(name);
                if (!node)
                    continue;

                bool ancestor_selected = false;
                for (NodeId check_id = node->parent_id; check_id != NULL_NODE;) {
                    if (selected_ids.count(check_id)) {
                        ancestor_selected = true;
                        break;
                    }
                    const auto* parent = scene.getNodeById(check_id);
                    check_id = parent ? parent->parent_id : NULL_NODE;
                }

                if (!ancestor_selected) {
                    node_gizmo_node_names_.push_back(name);
                }
            }

            node_transforms_before_drag_.clear();
            node_original_world_positions_.clear();
            node_parent_world_inverses_.clear();
            node_original_rotations_.clear();
            node_original_scales_.clear();

            for (const auto& name : node_gizmo_node_names_) {
                const auto* node = scene.getNode(name);
                if (!node)
                    continue;

                const glm::mat4 world_t = scene.getWorldTransform(node->id);
                const glm::mat4 local_t = node->local_transform.get();
                node_transforms_before_drag_.push_back(local_t);
                node_original_rotations_.push_back(extractRotation(local_t));
                node_original_scales_.push_back(extractScale(local_t));

                glm::mat4 parent_world(1.0f);
                if (node->parent_id != NULL_NODE) {
                    parent_world = scene.getWorldTransform(node->parent_id);
                }

                node_original_world_positions_.emplace_back(world_t[3]);
                node_parent_world_inverses_.push_back(glm::inverse(parent_world));
            }
        }

        if (gizmo_changed && is_using) {
            if (is_multi_selection) {
                if (node_gizmo_operation_ == ImGuizmo::TRANSLATE) {
                    const glm::vec3 new_gizmo_pos(gizmo_matrix[3]);
                    const glm::vec3 delta = new_gizmo_pos - gizmo_pivot_;

                    for (size_t i = 0; i < node_gizmo_node_names_.size(); ++i) {
                        const glm::mat4& original_local = node_transforms_before_drag_[i];
                        const glm::vec3& original_world_pos = node_original_world_positions_[i];
                        const glm::mat4& parent_inv = node_parent_world_inverses_[i];

                        const glm::vec3 new_world_pos = original_world_pos + delta;
                        const glm::vec3 new_local_pos = glm::vec3(parent_inv * glm::vec4(new_world_pos, 1.0f));

                        glm::mat4 new_transform = original_local;
                        new_transform[3] = glm::vec4(new_local_pos, 1.0f);
                        scene_manager->setNodeTransform(node_gizmo_node_names_[i], new_transform);
                    }
                } else if (node_gizmo_operation_ == ImGuizmo::ROTATE) {
                    const glm::mat3 delta_rot = extractRotation(delta_matrix);
                    gizmo_cumulative_rotation_ = delta_rot * gizmo_cumulative_rotation_;

                    for (size_t i = 0; i < node_gizmo_node_names_.size(); ++i) {
                        const glm::vec3& original_world_pos = node_original_world_positions_[i];
                        const glm::mat4& parent_inv = node_parent_world_inverses_[i];
                        const glm::mat3& original_rot = node_original_rotations_[i];
                        const glm::vec3& original_scale = node_original_scales_[i];

                        const glm::vec3 offset = original_world_pos - gizmo_pivot_;
                        const glm::vec3 rotated_offset = gizmo_cumulative_rotation_ * offset;
                        const glm::vec3 new_world_pos = gizmo_pivot_ + rotated_offset;
                        const glm::vec3 new_local_pos = glm::vec3(parent_inv * glm::vec4(new_world_pos, 1.0f));

                        const glm::mat3 parent_rot = extractRotation(glm::inverse(parent_inv));
                        const glm::mat3 parent_rot_inv = glm::transpose(parent_rot);
                        const glm::mat3 local_delta_rot = parent_rot_inv * gizmo_cumulative_rotation_ * parent_rot;
                        const glm::mat3 new_rot = local_delta_rot * original_rot;

                        glm::mat4 new_transform(1.0f);
                        new_transform[0] = glm::vec4(new_rot[0] * original_scale.x, 0.0f);
                        new_transform[1] = glm::vec4(new_rot[1] * original_scale.y, 0.0f);
                        new_transform[2] = glm::vec4(new_rot[2] * original_scale.z, 0.0f);
                        new_transform[3] = glm::vec4(new_local_pos, 1.0f);
                        scene_manager->setNodeTransform(node_gizmo_node_names_[i], new_transform);
                    }
                } else if (node_gizmo_operation_ == ImGuizmo::SCALE) {
                    gizmo_cumulative_scale_ *= extractScale(delta_matrix);

                    const glm::mat3 world_scale(gizmo_cumulative_scale_.x, 0.0f, 0.0f,
                                                0.0f, gizmo_cumulative_scale_.y, 0.0f,
                                                0.0f, 0.0f, gizmo_cumulative_scale_.z);

                    for (size_t i = 0; i < node_gizmo_node_names_.size(); ++i) {
                        const glm::vec3& original_world_pos = node_original_world_positions_[i];
                        const glm::mat4& parent_inv = node_parent_world_inverses_[i];
                        const glm::mat3& original_rot = node_original_rotations_[i];
                        const glm::vec3& original_scale = node_original_scales_[i];

                        const glm::vec3 offset = original_world_pos - gizmo_pivot_;
                        const glm::vec3 new_world_pos = gizmo_pivot_ + offset * gizmo_cumulative_scale_;
                        const glm::vec3 new_local_pos = glm::vec3(parent_inv * glm::vec4(new_world_pos, 1.0f));

                        const glm::mat3 parent_rot_inv = extractRotation(parent_inv);
                        const glm::mat3 parent_rot = glm::transpose(parent_rot_inv);
                        const glm::mat3 local_scale = parent_rot_inv * world_scale * parent_rot;

                        const glm::mat3 original_rs(original_rot[0] * original_scale.x,
                                                    original_rot[1] * original_scale.y,
                                                    original_rot[2] * original_scale.z);
                        const glm::mat3 new_rs = local_scale * original_rs;

                        const glm::mat4 new_transform(glm::vec4(new_rs[0], 0.0f),
                                                      glm::vec4(new_rs[1], 0.0f),
                                                      glm::vec4(new_rs[2], 0.0f),
                                                      glm::vec4(new_local_pos, 1.0f));
                        scene_manager->setNodeTransform(node_gizmo_node_names_[i], new_transform);
                    }
                }
            } else {
                // Single selection
                const glm::mat4 node_transform = scene_manager->getSelectedNodeTransform();
                const glm::vec3 new_gizmo_pos_world = glm::vec3(gizmo_matrix[3]);

                // Convert world position to parent space
                const auto& sm_scene = scene_manager->getScene();
                const auto* node = sm_scene.getNode(*selected_names.begin());
                const glm::mat4 parent_world_inv = (node && node->parent_id != NULL_NODE)
                                                       ? glm::inverse(sm_scene.getWorldTransform(node->parent_id))
                                                       : glm::mat4(1.0f);
                const glm::vec3 new_gizmo_pos = glm::vec3(parent_world_inv * glm::vec4(new_gizmo_pos_world, 1.0f));

                glm::mat4 new_transform;
                if (use_world_space) {
                    const glm::mat3 old_rs(node_transform);
                    const glm::mat3 delta_rs(delta_matrix);
                    const glm::mat3 parent_rot_inv = extractRotation(parent_world_inv);
                    const glm::mat3 parent_rot = glm::transpose(parent_rot_inv);
                    const glm::mat3 local_delta = parent_rot_inv * delta_rs * parent_rot;
                    const glm::mat3 new_rs = local_delta * old_rs;
                    new_transform = glm::mat4(new_rs);
                    new_transform[3] = glm::vec4(new_gizmo_pos - new_rs * local_pivot, 1.0f);
                } else {
                    const glm::mat3 new_rs(gizmo_matrix);
                    new_transform = gizmo_matrix;
                    new_transform[3] = glm::vec4(new_gizmo_pos - new_rs * local_pivot, 1.0f);
                }
                scene_manager->setSelectedNodeTransform(new_transform);
            }
        }

        // Create undo command when drag ends
        if (!is_using && node_gizmo_active_) {
            node_gizmo_active_ = false;
            // Clear child cropbox/ellipsoid gizmo state
            if (render_manager) {
                render_manager->setCropboxGizmoActive(false);
                render_manager->setEllipsoidGizmoActive(false);
            }

            const size_t count = node_gizmo_node_names_.size();
            std::vector<glm::mat4> final_transforms;
            final_transforms.reserve(count);
            for (const auto& name : node_gizmo_node_names_) {
                final_transforms.push_back(scene_manager->getNodeTransform(name));
            }

            bool any_changed = false;
            for (size_t i = 0; i < count; ++i) {
                if (node_transforms_before_drag_[i] != final_transforms[i]) {
                    any_changed = true;
                    break;
                }
            }

            if (any_changed) {
                op::OperatorProperties props;
                props.set("node_names", node_gizmo_node_names_);
                props.set("old_transforms", node_transforms_before_drag_);
                op::operators().invoke(op::BuiltinOp::TransformApplyBatch, &props);
            }
        }

        // Sync child cropbox/ellipsoid state when parent splat is being transformed
        if (node_gizmo_active_ && render_manager) {
            for (const auto& name : selected_names) {
                const auto* node = scene.getNode(name);
                if (!node || node->type != NodeType::SPLAT)
                    continue;

                // Find cropbox child
                const NodeId cropbox_id = scene.getCropBoxForSplat(node->id);
                if (cropbox_id != NULL_NODE) {
                    const auto* cropbox_node = scene.getNodeById(cropbox_id);
                    if (cropbox_node && cropbox_node->cropbox) {
                        const glm::mat4 cropbox_world = scene.getWorldTransform(cropbox_id);
                        render_manager->setCropboxGizmoState(true, cropbox_node->cropbox->min,
                                                             cropbox_node->cropbox->max, cropbox_world);
                    }
                }

                // Find ellipsoid child
                const NodeId ellipsoid_id = scene.getEllipsoidForSplat(node->id);
                if (ellipsoid_id != NULL_NODE) {
                    const auto* ellipsoid_node = scene.getNodeById(ellipsoid_id);
                    if (ellipsoid_node && ellipsoid_node->ellipsoid) {
                        const glm::mat4 ellipsoid_world = scene.getWorldTransform(ellipsoid_id);
                        render_manager->setEllipsoidGizmoState(true, ellipsoid_node->ellipsoid->radii,
                                                               ellipsoid_world);
                    }
                }
            }
        }

        overlay_drawlist->PopClipRect();
    }

    void GuiManager::performExport(ExportFormat format, const std::filesystem::path& path,
                                   const std::vector<std::string>& node_names, int sh_degree) {
        if (isExporting())
            return;

        auto* const scene_manager = viewer_->getSceneManager();
        if (!scene_manager || node_names.empty())
            return;

        const auto& scene = scene_manager->getScene();
        std::vector<std::pair<const lfs::core::SplatData*, glm::mat4>> splats;
        splats.reserve(node_names.size());
        for (const auto& name : node_names) {
            const auto* node = scene.getNode(name);
            if (node && node->type == NodeType::SPLAT && node->model) {
                splats.emplace_back(node->model.get(), scene.getWorldTransform(node->id));
            }
        }
        if (splats.empty())
            return;

        auto merged = Scene::mergeSplatsWithTransforms(splats);
        if (!merged)
            return;

        if (sh_degree < merged->get_max_sh_degree()) {
            truncateSHDegree(*merged, sh_degree);
        }

        startAsyncExport(format, path, std::move(merged));
    }

    void GuiManager::startAsyncExport(ExportFormat format,
                                      const std::filesystem::path& path,
                                      std::unique_ptr<lfs::core::SplatData> data) {
        if (!data) {
            LOG_ERROR("No splat data to export");
            return;
        }

        export_state_.active.store(true);
        export_state_.cancel_requested.store(false);
        export_state_.progress.store(0.0f);
        {
            const std::lock_guard lock(export_state_.mutex);
            export_state_.format = format;
            export_state_.stage = "Starting";
            export_state_.error.clear();
        }

        auto splat_data = std::make_shared<lfs::core::SplatData>(std::move(*data));
        LOG_INFO("Export started: {} (format: {})", lfs::core::path_to_utf8(path), static_cast<int>(format));

        export_state_.thread = std::make_unique<std::jthread>(
            [this, format, path, splat_data](std::stop_token stop_token) {
                auto update_progress = [this, &stop_token](float progress, const std::string& stage) -> bool {
                    export_state_.progress.store(progress);
                    {
                        const std::lock_guard lock(export_state_.mutex);
                        export_state_.stage = stage;
                    }
                    if (stop_token.stop_requested() || export_state_.cancel_requested.load()) {
                        LOG_INFO("Export cancelled");
                        return false;
                    }
                    return true;
                };

                bool success = false;
                std::string error_msg;

                switch (format) {
                case ExportFormat::PLY: {
                    update_progress(0.1f, "Writing PLY");
                    const lfs::io::PlySaveOptions options{
                        .output_path = path,
                        .binary = true,
                        .async = false};
                    if (auto result = lfs::io::save_ply(*splat_data, options); result) {
                        success = true;
                        update_progress(1.0f, "Complete");
                    } else {
                        error_msg = result.error().message;
                        // Check if this is a disk space error
                        if (result.error().code == lfs::io::ErrorCode::INSUFFICIENT_DISK_SPACE) {
                            // Emit event for disk space error dialog
                            lfs::core::events::state::DiskSpaceSaveFailed{
                                .iteration = 0,
                                .path = path,
                                .error = result.error().message,
                                .required_bytes = result.error().required_bytes,
                                .available_bytes = result.error().available_bytes,
                                .is_disk_space_error = true,
                                .is_checkpoint = false}
                                .emit();
                        }
                    }
                    break;
                }
                case ExportFormat::SOG: {
                    const lfs::io::SogSaveOptions options{
                        .output_path = path,
                        .kmeans_iterations = 10,
                        .progress_callback = update_progress};
                    if (auto result = lfs::io::save_sog(*splat_data, options); result) {
                        success = true;
                    } else {
                        error_msg = result.error().message;
                    }
                    break;
                }
                case ExportFormat::SPZ: {
                    update_progress(0.1f, "Writing SPZ");
                    const lfs::io::SpzSaveOptions options{.output_path = path};
                    if (auto result = lfs::io::save_spz(*splat_data, options); result) {
                        success = true;
                        update_progress(1.0f, "Complete");
                    } else {
                        error_msg = result.error().message;
                    }
                    break;
                }
                case ExportFormat::HTML_VIEWER: {
                    const HtmlViewerExportOptions options{
                        .output_path = path,
                        .progress_callback = [&update_progress](float p, const std::string& s) {
                            update_progress(p, s);
                        }};
                    if (auto result = export_html_viewer(*splat_data, options); result) {
                        success = true;
                    } else {
                        error_msg = result.error();
                    }
                    break;
                }
                }

                if (success) {
                    LOG_INFO("Export completed: {}", lfs::core::path_to_utf8(path));
                    const std::lock_guard lock(export_state_.mutex);
                    export_state_.stage = "Complete";
                } else {
                    LOG_ERROR("Export failed: {}", error_msg);
                    const std::lock_guard lock(export_state_.mutex);
                    export_state_.error = error_msg;
                    export_state_.stage = "Failed";
                }

                export_state_.active.store(false);
            });
    }

    void GuiManager::cancelExport() {
        if (!export_state_.active.load())
            return;

        LOG_INFO("Cancelling export");
        export_state_.cancel_requested.store(true);
        if (export_state_.thread && export_state_.thread->joinable()) {
            export_state_.thread->request_stop();
        }
    }

    void GuiManager::startAsyncImport(const std::filesystem::path& path,
                                      const lfs::core::param::TrainingParameters& params) {
        if (import_state_.active.load()) {
            LOG_WARN("Import already in progress");
            return;
        }

        import_state_.active.store(true);
        import_state_.load_complete.store(false);
        import_state_.show_completion.store(false);
        import_state_.progress.store(0.0f);
        {
            const std::lock_guard lock(import_state_.mutex);
            import_state_.path = path;
            import_state_.stage = "Initializing...";
            import_state_.error.clear();
            import_state_.num_images = 0;
            import_state_.num_points = 0;
            import_state_.success = false;
            import_state_.load_result.reset();
            import_state_.params = params;
            import_state_.dataset_type = getDatasetTypeName(path);
        }

        LOG_INFO("Async import: {}", lfs::core::path_to_utf8(path));

        import_state_.thread = std::make_unique<std::jthread>(
            [this, path](const std::stop_token stop_token) {
                lfs::core::param::TrainingParameters local_params;
                {
                    const std::lock_guard lock(import_state_.mutex);
                    local_params = import_state_.params;
                }

                const lfs::io::LoadOptions load_options{
                    .resize_factor = local_params.dataset.resize_factor,
                    .max_width = local_params.dataset.max_width,
                    .images_folder = local_params.dataset.images,
                    .validate_only = false,
                    .progress = [this, &stop_token](const float pct, const std::string& msg) {
                        if (stop_token.stop_requested())
                            return;
                        import_state_.progress.store(pct / 100.0f);
                        const std::lock_guard lock(import_state_.mutex);
                        import_state_.stage = msg;
                    }};

                auto loader = lfs::io::Loader::create();
                auto result = loader->load(path, load_options);

                if (stop_token.stop_requested()) {
                    import_state_.active.store(false);
                    return;
                }

                const std::lock_guard lock(import_state_.mutex);
                if (result) {
                    import_state_.load_result = std::move(*result);
                    import_state_.success = true;
                    import_state_.stage = "Applying...";
                    std::visit([this](const auto& data) {
                        using T = std::decay_t<decltype(data)>;
                        if constexpr (std::is_same_v<T, std::shared_ptr<lfs::core::SplatData>>) {
                            import_state_.num_points = data->size();
                            import_state_.num_images = 0;
                        } else if constexpr (std::is_same_v<T, lfs::io::LoadedScene>) {
                            import_state_.num_images = data.cameras.size();
                            import_state_.num_points = data.point_cloud ? data.point_cloud->size() : 0;
                        }
                    },
                               import_state_.load_result->data);
                } else {
                    import_state_.success = false;
                    import_state_.error = result.error().format();
                    import_state_.stage = "Failed";
                    LOG_ERROR("Import failed: {}", import_state_.error);
                }
                import_state_.progress.store(1.0f);
                import_state_.load_complete.store(true);
            });
    }

    void GuiManager::checkAsyncImportCompletion() {
        if (!import_state_.load_complete.load()) {
            return;
        }
        import_state_.load_complete.store(false);

        bool success;
        {
            const std::lock_guard lock(import_state_.mutex);
            success = import_state_.success;
        }

        if (success) {
            applyLoadedDataToScene();
        } else {
            import_state_.active.store(false);
            import_state_.show_completion.store(true);
            const std::lock_guard lock(import_state_.mutex);
            import_state_.completion_time = std::chrono::steady_clock::now();
        }

        if (import_state_.thread && import_state_.thread->joinable()) {
            import_state_.thread->join();
            import_state_.thread.reset();
        }
    }

    void GuiManager::applyLoadedDataToScene() {
        auto* const scene_manager = viewer_->getSceneManager();
        if (!scene_manager) {
            LOG_ERROR("No scene manager");
            import_state_.active.store(false);
            return;
        }

        std::optional<lfs::io::LoadResult> load_result;
        lfs::core::param::TrainingParameters params;
        std::filesystem::path path;
        {
            const std::lock_guard lock(import_state_.mutex);
            load_result = std::move(import_state_.load_result);
            params = import_state_.params;
            path = import_state_.path;
            import_state_.load_result.reset();
        }

        if (!load_result) {
            LOG_ERROR("No load result");
            import_state_.active.store(false);
            return;
        }

        const auto result = scene_manager->applyLoadedDataset(path, params, std::move(*load_result));

        {
            const std::lock_guard lock(import_state_.mutex);
            import_state_.completion_time = std::chrono::steady_clock::now();
            import_state_.success = result.has_value();
            import_state_.stage = result ? "Complete" : "Failed";
            if (!result) {
                import_state_.error = result.error();
            }
        }

        import_state_.active.store(false);
        import_state_.show_completion.store(true);

        lfs::core::events::state::DatasetLoadCompleted{
            .path = path,
            .success = import_state_.success,
            .error = import_state_.success ? std::nullopt : std::optional<std::string>(import_state_.error),
            .num_images = import_state_.num_images,
            .num_points = import_state_.num_points}
            .emit();
    }

    void GuiManager::cancelVideoExport() {
        if (!video_export_state_.active.load())
            return;

        LOG_INFO("Cancelling video export");
        video_export_state_.cancel_requested.store(true);
        if (video_export_state_.thread) {
            video_export_state_.thread->request_stop();
        }
    }

    void GuiManager::startVideoExport(const std::filesystem::path& path,
                                      const io::video::VideoExportOptions& options) {
        auto* const scene_manager = viewer_->getSceneManager();
        auto* const rendering_manager = viewer_->getRenderingManager();
        if (!scene_manager || !rendering_manager) {
            LOG_ERROR("Cannot export video: missing components");
            return;
        }

        const auto& timeline = sequencer_controller_.timeline();
        if (timeline.empty()) {
            LOG_ERROR("Cannot export video: no keyframes");
            return;
        }

        // Get scene render state
        const auto render_state = scene_manager->buildRenderState();
        if (!render_state.combined_model) {
            LOG_ERROR("No splat data to render");
            return;
        }

        // Get rendering engine
        auto* const engine = rendering_manager->getRenderingEngine();
        if (!engine) {
            LOG_ERROR("Rendering engine not available");
            return;
        }

        const float duration = timeline.duration();
        const int total_frames = static_cast<int>(std::ceil(duration * options.framerate)) + 1;
        const int width = options.width;
        const int height = options.height;

        video_export_state_.active.store(true);
        video_export_state_.cancel_requested.store(false);
        video_export_state_.progress.store(0.0f);
        video_export_state_.total_frames.store(total_frames);
        video_export_state_.current_frame.store(0);
        {
            std::lock_guard lock(video_export_state_.mutex);
            video_export_state_.stage = "Initializing";
            video_export_state_.error.clear();
        }

        LOG_INFO("Starting video export: {} frames at {}x{}", total_frames, width, height);

        // Capture settings from rendering manager
        const auto render_settings = rendering_manager->getSettings();
        const lfs::core::SplatData* splat_ptr = render_state.combined_model;

        video_export_state_.thread = std::make_unique<std::jthread>(
            [this, path, options, total_frames, width, height,
             splat_ptr, engine, render_settings](std::stop_token stop_token) {
                io::video::VideoEncoder encoder;

                {
                    std::lock_guard lock(video_export_state_.mutex);
                    video_export_state_.stage = "Opening encoder";
                }

                auto result = encoder.open(path, options);
                if (!result) {
                    std::lock_guard lock(video_export_state_.mutex);
                    video_export_state_.error = result.error();
                    video_export_state_.stage = "Failed: " + result.error();
                    video_export_state_.active.store(false);
                    LOG_ERROR("Failed to open encoder: {}", result.error());
                    return;
                }

                const float start_time = sequencer_controller_.timeline().startTime();
                const float time_step = 1.0f / static_cast<float>(options.framerate);

                for (int frame = 0; frame < total_frames; ++frame) {
                    if (stop_token.stop_requested() || video_export_state_.cancel_requested.load()) {
                        LOG_INFO("Video export cancelled at frame {}", frame);
                        break;
                    }

                    const float time = start_time + static_cast<float>(frame) * time_step;
                    const auto cam_state = sequencer_controller_.timeline().evaluate(time);

                    // Create render request
                    rendering::RenderRequest request;
                    request.viewport.rotation = glm::mat3_cast(cam_state.rotation);
                    request.viewport.translation = cam_state.position;
                    request.viewport.size = {width, height};
                    request.viewport.focal_length_mm = lfs::rendering::vFovToFocalLength(cam_state.fov);
                    request.background_color = render_settings.background_color;
                    request.sh_degree = render_settings.sh_degree;
                    request.scaling_modifier = render_settings.scaling_modifier;
                    request.antialiasing = true;

                    auto render_result = engine->renderGaussians(*splat_ptr, request);
                    if (!render_result.has_value() || !render_result->valid || !render_result->image) {
                        LOG_ERROR("Failed to render frame {}", frame);
                        continue;
                    }

                    // Convert from CHW (channel-first) to HWC (height-width-channel) for video encoding
                    // Renderer outputs [C, H, W], encoder expects [H, W, C]
                    auto image_hwc = render_result->image->permute({1, 2, 0}).contiguous();

                    // Debug: log tensor info on first frame
                    if (frame == 0) {
                        LOG_INFO("Video export: CHW shape=[{},{},{}] -> HWC shape=[{},{},{}]",
                                 render_result->image->shape()[0], render_result->image->shape()[1], render_result->image->shape()[2],
                                 image_hwc.shape()[0], image_hwc.shape()[1], image_hwc.shape()[2]);
                    }

                    // Encode directly from GPU (NVENC or CUDA color convert + x264)
                    const auto* const gpu_ptr = image_hwc.data_ptr();
                    auto write_result = encoder.writeFrameGpu(gpu_ptr, width, height, nullptr);
                    if (!write_result) {
                        std::lock_guard lock(video_export_state_.mutex);
                        video_export_state_.error = write_result.error();
                        video_export_state_.stage = "Encode error";
                        LOG_ERROR("Failed to encode frame {}: {}", frame, write_result.error());
                        break;
                    }

                    video_export_state_.current_frame.store(frame + 1);
                    video_export_state_.progress.store(
                        static_cast<float>(frame + 1) / static_cast<float>(total_frames));
                    {
                        std::lock_guard lock(video_export_state_.mutex);
                        video_export_state_.stage = std::format("Encoding frame {}/{}", frame + 1, total_frames);
                    }
                }

                {
                    std::lock_guard lock(video_export_state_.mutex);
                    video_export_state_.stage = "Finalizing";
                }

                if (auto close_result = encoder.close(); !close_result) {
                    std::lock_guard lock(video_export_state_.mutex);
                    video_export_state_.error = close_result.error();
                    video_export_state_.stage = "Failed";
                    LOG_ERROR("Failed to close encoder: {}", close_result.error());
                } else if (video_export_state_.error.empty() && !video_export_state_.cancel_requested.load()) {
                    std::lock_guard lock(video_export_state_.mutex);
                    video_export_state_.stage = "Complete";
                    LOG_INFO("Video export completed: {}", path.string());
                }

                video_export_state_.active.store(false);
            });
    }

    void GuiManager::renderStartupOverlay() {
        if (!show_startup_overlay_)
            return;

        static constexpr float MIN_VIEWPORT_SIZE = 100.0f;
        if (viewport_size_.x < MIN_VIEWPORT_SIZE || viewport_size_.y < MIN_VIEWPORT_SIZE)
            return;

        // Layout constants
        static constexpr float MAIN_LOGO_SCALE = 1.3f;
        static constexpr float CORE11_LOGO_SCALE = 0.5f;
        static constexpr float CORNER_RADIUS = 12.0f;
        static constexpr float PADDING_X = 40.0f;
        static constexpr float PADDING_Y = 28.0f;
        static constexpr float GAP_LOGO_TEXT = 20.0f;
        static constexpr float GAP_TEXT_CORE11 = 10.0f;
        static constexpr float GAP_CORE11_HINT = 16.0f;
        static constexpr float GAP_LANG_HINT = 12.0f;
        static constexpr float LANG_COMBO_WIDTH = 140.0f;

        const auto& t = theme();
        const bool is_dark_theme = (t.name == "Dark");
        const unsigned int logo_texture = is_dark_theme ? startup_logo_light_texture_ : startup_logo_dark_texture_;
        const unsigned int core11_texture = is_dark_theme ? startup_core11_light_texture_ : startup_core11_dark_texture_;

        const float main_logo_w = static_cast<float>(startup_logo_width_) * MAIN_LOGO_SCALE;
        const float main_logo_h = static_cast<float>(startup_logo_height_) * MAIN_LOGO_SCALE;
        const float core11_w = static_cast<float>(startup_core11_width_) * CORE11_LOGO_SCALE;
        const float core11_h = static_cast<float>(startup_core11_height_) * CORE11_LOGO_SCALE;

        // Text sizes (use localized strings)
        const char* supported_text = LOC(lichtfeld::Strings::Startup::SUPPORTED_BY);
        const char* click_hint = LOC(lichtfeld::Strings::Startup::CLICK_TO_CONTINUE);
        if (font_small_)
            ImGui::PushFont(font_small_);
        const ImVec2 supported_size = ImGui::CalcTextSize(supported_text);
        const ImVec2 hint_size = ImGui::CalcTextSize(click_hint);
        const ImVec2 lang_label_size = ImGui::CalcTextSize(LOC(lichtfeld::Strings::Preferences::LANGUAGE));
        if (font_small_)
            ImGui::PopFont();

        // Overlay dimensions (include language selector height)
        const float lang_row_height = ImGui::GetFrameHeight() + 4.0f;
        const float content_width = std::max({main_logo_w, core11_w, supported_size.x, hint_size.x, LANG_COMBO_WIDTH + lang_label_size.x + 8.0f});
        const float content_height = main_logo_h + GAP_LOGO_TEXT + supported_size.y + GAP_TEXT_CORE11 +
                                     core11_h + GAP_CORE11_HINT + lang_row_height + GAP_LANG_HINT + hint_size.y;
        const float overlay_width = content_width + PADDING_X * 2.0f;
        const float overlay_height = content_height + PADDING_Y * 2.0f;

        // Center in viewport
        const float center_x = viewport_pos_.x + viewport_size_.x * 0.5f;
        const float center_y = viewport_pos_.y + viewport_size_.y * 0.5f;
        const ImVec2 overlay_pos(center_x - overlay_width * 0.5f, center_y - overlay_height * 0.5f);

        // Style the overlay window
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CORNER_RADIUS);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {PADDING_X, PADDING_Y});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, t.palette.surface);
        ImGui::PushStyleColor(ImGuiCol_Border, t.palette.border);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, t.palette.background);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, lighten(t.palette.background, 0.05f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, lighten(t.palette.background, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, t.palette.surface);
        ImGui::PushStyleColor(ImGuiCol_Header, t.palette.primary);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, lighten(t.palette.primary, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, t.palette.primary);

        ImGui::SetNextWindowPos(overlay_pos);
        ImGui::SetNextWindowSize({overlay_width, overlay_height});

        if (ImGui::Begin("##StartupOverlay", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoCollapse)) {

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const float window_center_x = window_pos.x + overlay_width * 0.5f;
            float y = window_pos.y + PADDING_Y;

            // Main logo
            if (logo_texture && startup_logo_width_ > 0) {
                const float x = window_center_x - main_logo_w * 0.5f;
                draw_list->AddImage(static_cast<ImTextureID>(logo_texture),
                                    {x, y}, {x + main_logo_w, y + main_logo_h});
                y += main_logo_h + GAP_LOGO_TEXT;
            }

            // Supported by text
            if (font_small_)
                ImGui::PushFont(font_small_);
            draw_list->AddText({window_center_x - supported_size.x * 0.5f, y},
                               toU32WithAlpha(t.palette.text_dim, 0.85f), supported_text);
            y += supported_size.y + GAP_TEXT_CORE11;

            // Core11 logo
            if (core11_texture && startup_core11_width_ > 0) {
                const float x = window_center_x - core11_w * 0.5f;
                draw_list->AddImage(static_cast<ImTextureID>(core11_texture),
                                    {x, y}, {x + core11_w, y + core11_h});
                y += core11_h + GAP_CORE11_HINT;
            }

            // Language selector - center the row in content area
            const float lang_total_width = lang_label_size.x + 8.0f + LANG_COMBO_WIDTH;
            const float content_area_width = overlay_width - 2.0f * PADDING_X;
            const float lang_indent = (content_area_width - lang_total_width) * 0.5f;
            ImGui::SetCursorPosY(y - window_pos.y);
            ImGui::SetCursorPosX(lang_indent);
            ImGui::TextColored(t.palette.text_dim, "%s", LOC(lichtfeld::Strings::Preferences::LANGUAGE));
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetNextItemWidth(LANG_COMBO_WIDTH);

            auto& loc = lfs::event::LocalizationManager::getInstance();
            const auto& current_lang = loc.getCurrentLanguage();
            const auto available_langs = loc.getAvailableLanguages();
            const auto lang_names = loc.getAvailableLanguageNames();

            // Find current language name for preview
            std::string current_name = current_lang;
            for (size_t i = 0; i < available_langs.size(); ++i) {
                if (available_langs[i] == current_lang) {
                    current_name = lang_names[i];
                    break;
                }
            }

            if (ImGui::BeginCombo("##LangCombo", current_name.c_str())) {
                for (size_t i = 0; i < available_langs.size(); ++i) {
                    const bool is_selected = (available_langs[i] == current_lang);
                    if (ImGui::Selectable(lang_names[i].c_str(), is_selected)) {
                        loc.setLanguage(available_langs[i]);
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            y += lang_row_height + GAP_LANG_HINT;

            // Dismiss hint
            draw_list->AddText({window_center_x - hint_size.x * 0.5f, y},
                               toU32WithAlpha(t.palette.text_dim, 0.5f), click_hint);
            if (font_small_)
                ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleColor(9);
        ImGui::PopStyleVar(5);

        // Dismiss on user interaction (but not when interacting with language combo or modals)
        const auto& io = ImGui::GetIO();
        const bool mouse_action = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                                  ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                                  std::abs(io.MouseWheel) > 0.0f || std::abs(io.MouseWheelH) > 0.0f;
        const bool key_action = io.InputQueueCharacters.Size > 0 ||
                                ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                                ImGui::IsKeyPressed(ImGuiKey_Space) ||
                                ImGui::IsKeyPressed(ImGuiKey_Enter);

        // Don't dismiss if interacting with language combo or any popup/modal
        const bool any_popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool any_item_active = ImGui::IsAnyItemActive();
        if (!any_popup_open && !any_item_active && !drag_drop_hovering_ && (mouse_action || key_action)) {
            show_startup_overlay_ = false;
        }
    }

    void GuiManager::requestExitConfirmation() {
        lfs::core::events::cmd::RequestExit{}.emit();
    }

    bool GuiManager::isExitConfirmationPending() const {
        return lfs::python::is_exit_popup_open();
    }

} // namespace lfs::vis::gui
