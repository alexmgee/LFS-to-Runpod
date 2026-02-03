/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "video_extractor_dialog.hpp"
#include "gui/utils/windows_utils.hpp"
#include "theme/theme.hpp"

#include <array>
#include <cmath>
#include <format>
#include <imgui.h>

using lfs::vis::gui::OpenVideoFileDialog;
using lfs::vis::gui::SelectFolderDialog;

namespace lfs::gui {

    namespace {

        constexpr float BUTTON_SIZE = 28.0f;
        constexpr float BUTTON_SPACING = 4.0f;
        constexpr float ICON_SIZE = 7.0f;
        constexpr float PLAY_ICON_SIZE = 8.0f;
        constexpr float PAUSE_BAR_W = 2.5f;
        constexpr float PAUSE_BAR_H = 9.0f;
        constexpr float PAUSE_GAP = 3.0f;
        constexpr float SKIP_ICON_SIZE = 5.0f;
        constexpr float TIMELINE_HEIGHT = 20.0f;
        constexpr float PLAYHEAD_HANDLE_SIZE = 6.0f;
        constexpr float MIN_TICK_SPACING = 4.0f;
        constexpr float MARKER_HEIGHT = 6.0f;

        [[nodiscard]] std::string formatTime(const double seconds) {
            const int mins = static_cast<int>(seconds) / 60;
            const double secs = seconds - static_cast<double>(mins * 60);
            return std::format("{}:{:05.2f}", mins, secs);
        }

    } // namespace

    VideoExtractorDialog::VideoExtractorDialog() : player_(std::make_unique<io::VideoPlayer>()) {}

    VideoExtractorDialog::~VideoExtractorDialog() {
        if (preview_texture_ != 0) {
            glDeleteTextures(1, &preview_texture_);
        }
    }

    void VideoExtractorDialog::setOnStartExtraction(
        std::function<void(const VideoExtractionParams&)> callback) {
        on_start_extraction_ = std::move(callback);
    }

    void VideoExtractorDialog::updateProgress(int current, int total) {
        current_frame_.store(current);
        total_frames_.store(total);
    }

    void VideoExtractorDialog::setExtractionComplete() {
        extracting_.store(false);
        error_message_.clear();
        show_completion_message_ = true;
    }

    void VideoExtractorDialog::setExtractionError(const std::string& error) {
        extracting_.store(false);
        error_message_ = error;
    }

    bool VideoExtractorDialog::isVideoPlaying() const {
        return player_ && player_->isPlaying();
    }

    void VideoExtractorDialog::openVideo(const std::filesystem::path& path) {
        if (player_->open(path)) {
            video_path_ = path;
            trim_start_ = 0.0f;
            trim_end_ = static_cast<float>(player_->duration());
            texture_needs_update_ = true;

            // Auto-set output directory
            if (output_dir_.empty()) {
                output_dir_ = video_path_.parent_path() / (video_path_.stem().string() + "_frames");
            }
        }
    }

    int VideoExtractorDialog::calculateEstimatedFrames() const {
        if (!player_ || !player_->isOpen()) {
            return 0;
        }
        const double trim_duration = static_cast<double>(trim_end_ - trim_start_);
        if (mode_selection_ == 0) {
            return static_cast<int>(trim_duration * static_cast<double>(fps_));
        }
        const double video_fps = player_->fps();
        return static_cast<int>((trim_duration * video_fps) / frame_interval_);
    }

    void VideoExtractorDialog::renderExtractionMarkers(
        ImDrawList* const dl, const ImVec2 pos, const float width, const float height, const double duration) {

        const int frame_count = calculateEstimatedFrames();
        if (frame_count <= 0) {
            return;
        }

        const float dur_f = static_cast<float>(duration);
        const float trim_start_x = pos.x + (trim_start_ / dur_f) * width;
        const float trim_end_x = pos.x + (trim_end_ / dur_f) * width;
        const float pixels_per_frame = (trim_end_x - trim_start_x) / static_cast<float>(frame_count);

        const auto& t = lfs::vis::theme();
        const float y_top = pos.y + height - MARKER_HEIGHT;
        const float y_bot = pos.y + height - 1.0f;

        if (pixels_per_frame >= MIN_TICK_SPACING) {
            const ImU32 color = lfs::vis::toU32WithAlpha(t.palette.warning, 0.7f);
            const float time_step = (mode_selection_ == 0)
                                        ? 1.0f / fps_
                                        : static_cast<float>(frame_interval_) / static_cast<float>(player_->fps());
            const float px_per_sec = width / dur_f;

            for (int i = 0; i < frame_count; ++i) {
                const float x = pos.x + (trim_start_ + static_cast<float>(i) * time_step) * px_per_sec;
                dl->AddLine(ImVec2(x, y_top), ImVec2(x, y_bot), color, 1.0f);
            }
        } else {
            const ImU32 color = lfs::vis::toU32WithAlpha(t.palette.warning, 0.3f);
            dl->AddRectFilled(ImVec2(trim_start_x, y_top), ImVec2(trim_end_x, y_bot), color);
        }
    }

    void VideoExtractorDialog::updatePreviewTexture() {
        if (!player_->isOpen())
            return;

        const uint8_t* data = player_->currentFrameData();
        if (!data)
            return;

        const int width = player_->width();
        const int height = player_->height();

        // Create texture if needed
        if (preview_texture_ == 0) {
            glGenTextures(1, &preview_texture_);
            glBindTexture(GL_TEXTURE_2D, preview_texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glBindTexture(GL_TEXTURE_2D, preview_texture_);

        // Allocate or reallocate if size changed
        if (preview_texture_width_ != width || preview_texture_height_ != height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                         data);
            preview_texture_width_ = width;
            preview_texture_height_ = height;
        } else {
            // Direct upload - fast enough for 720p on modern GPUs
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, data);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        texture_needs_update_ = false;
    }

    void VideoExtractorDialog::renderVideoPreview() {
        // Update playback with wall clock time
        const double current_time = ImGui::GetTime();

        if (player_->isOpen()) {
            if (player_->update(current_time)) {
                texture_needs_update_ = true;
            }

            if (texture_needs_update_) {
                updatePreviewTexture();
            }
        }

        // Calculate preview area
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float preview_height = std::min(avail.x * 9.0f / 16.0f, 360.0f);

        ImGui::BeginChild("##preview_area", ImVec2(0, preview_height),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle);

        if (player_->isOpen() && preview_texture_ != 0) {
            const ImVec2 region = ImGui::GetContentRegionAvail();
            const float video_aspect =
                static_cast<float>(player_->width()) / static_cast<float>(player_->height());
            const float region_aspect = region.x / region.y;

            float display_width, display_height;
            if (video_aspect > region_aspect) {
                display_width = region.x;
                display_height = region.x / video_aspect;
            } else {
                display_height = region.y;
                display_width = region.y * video_aspect;
            }

            const float offset_x = (region.x - display_width) * 0.5f;
            const float offset_y = (region.y - display_height) * 0.5f;

            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset_x,
                                       ImGui::GetCursorPosY() + offset_y));
            ImGui::Image(static_cast<ImTextureID>(preview_texture_),
                         ImVec2(display_width, display_height));
        } else {
            const char* hint = "Select a video file to preview";
            const ImVec2 text_size = ImGui::CalcTextSize(hint);
            const ImVec2 region = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2((region.x - text_size.x) * 0.5f,
                                       (region.y - text_size.y) * 0.5f));
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", hint);
        }

        ImGui::EndChild();
    }

    void VideoExtractorDialog::renderTransportControls() {
        const auto& t = lfs::vis::theme();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, BUTTON_SIZE / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, t.button_normal());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.button_hovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.button_active());

        // |◀ Step backward
        if (ImGui::Button("##step_back", ImVec2(BUTTON_SIZE, BUTTON_SIZE))) {
            player_->stepBackward();
            texture_needs_update_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Step backward");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 btn_min = ImGui::GetItemRectMin();
            const ImVec2 center = ImVec2(btn_min.x + BUTTON_SIZE / 2, btn_min.y + BUTTON_SIZE / 2);
            dl->AddRectFilled(ImVec2(center.x - SKIP_ICON_SIZE - 1, center.y - SKIP_ICON_SIZE),
                              ImVec2(center.x - SKIP_ICON_SIZE + 1, center.y + SKIP_ICON_SIZE),
                              t.text_u32());
            dl->AddTriangleFilled(ImVec2(center.x + SKIP_ICON_SIZE, center.y - SKIP_ICON_SIZE),
                                  ImVec2(center.x + SKIP_ICON_SIZE, center.y + SKIP_ICON_SIZE),
                                  ImVec2(center.x - SKIP_ICON_SIZE + 2, center.y), t.text_u32());
        }

        ImGui::SameLine(0, BUTTON_SPACING);

        // ▶/❚❚ Play/Pause
        if (ImGui::Button("##playpause", ImVec2(BUTTON_SIZE, BUTTON_SIZE))) {
            player_->togglePlayPause();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(player_->isPlaying() ? "Pause (Space)" : "Play (Space)");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 btn_min = ImGui::GetItemRectMin();
            const ImVec2 center = ImVec2(btn_min.x + BUTTON_SIZE / 2, btn_min.y + BUTTON_SIZE / 2);

            if (player_->isPlaying()) {
                dl->AddRectFilled(ImVec2(center.x - PAUSE_GAP - PAUSE_BAR_W, center.y - PAUSE_BAR_H / 2),
                                  ImVec2(center.x - PAUSE_GAP, center.y + PAUSE_BAR_H / 2),
                                  t.text_u32());
                dl->AddRectFilled(
                    ImVec2(center.x + PAUSE_GAP - PAUSE_BAR_W, center.y - PAUSE_BAR_H / 2),
                    ImVec2(center.x + PAUSE_GAP, center.y + PAUSE_BAR_H / 2), t.text_u32());
            } else {
                dl->AddTriangleFilled(
                    ImVec2(center.x - PLAY_ICON_SIZE * 0.4f, center.y - PLAY_ICON_SIZE),
                    ImVec2(center.x - PLAY_ICON_SIZE * 0.4f, center.y + PLAY_ICON_SIZE),
                    ImVec2(center.x + PLAY_ICON_SIZE * 0.8f, center.y), t.text_u32());
            }
        }

        ImGui::SameLine(0, BUTTON_SPACING);

        // ▶| Step forward
        if (ImGui::Button("##step_fwd", ImVec2(BUTTON_SIZE, BUTTON_SIZE))) {
            player_->stepForward();
            texture_needs_update_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Step forward");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 btn_min = ImGui::GetItemRectMin();
            const ImVec2 center = ImVec2(btn_min.x + BUTTON_SIZE / 2, btn_min.y + BUTTON_SIZE / 2);
            dl->AddTriangleFilled(ImVec2(center.x - SKIP_ICON_SIZE, center.y - SKIP_ICON_SIZE),
                                  ImVec2(center.x - SKIP_ICON_SIZE, center.y + SKIP_ICON_SIZE),
                                  ImVec2(center.x + SKIP_ICON_SIZE - 2, center.y), t.text_u32());
            dl->AddRectFilled(ImVec2(center.x + SKIP_ICON_SIZE - 1, center.y - SKIP_ICON_SIZE),
                              ImVec2(center.x + SKIP_ICON_SIZE + 1, center.y + SKIP_ICON_SIZE),
                              t.text_u32());
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        // Time display
        ImGui::SameLine(0, 16.0f);
        if (player_->isOpen()) {
            ImGui::Text("%s / %s", formatTime(player_->currentTime()).c_str(),
                        formatTime(player_->duration()).c_str());
        } else {
            ImGui::TextDisabled("--:--.- / --:--.-");
        }
    }

    void VideoExtractorDialog::renderTimeline() {
        if (!player_->isOpen())
            return;

        const auto& t = lfs::vis::theme();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = TIMELINE_HEIGHT;

        // Timeline background
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                          lfs::vis::toU32WithAlpha(t.palette.background, 0.8f), 4.0f);
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                    lfs::vis::toU32WithAlpha(t.palette.border, 0.3f), 4.0f);

        const double duration = player_->duration();
        const double current = player_->currentTime();

        // Trim region highlight
        const float trim_start_x = pos.x + (trim_start_ / static_cast<float>(duration)) * width;
        const float trim_end_x = pos.x + (trim_end_ / static_cast<float>(duration)) * width;
        dl->AddRectFilled(ImVec2(trim_start_x, pos.y), ImVec2(trim_end_x, pos.y + height),
                          lfs::vis::toU32WithAlpha(t.palette.primary, 0.2f), 4.0f);

        // Trim markers
        dl->AddLine(ImVec2(trim_start_x, pos.y), ImVec2(trim_start_x, pos.y + height),
                    lfs::vis::toU32WithAlpha(t.palette.success, 0.8f), 2.0f);
        dl->AddLine(ImVec2(trim_end_x, pos.y), ImVec2(trim_end_x, pos.y + height),
                    lfs::vis::toU32WithAlpha(t.palette.error, 0.8f), 2.0f);

        // Extraction frame markers
        renderExtractionMarkers(dl, pos, width, height, duration);

        // Progress bar
        const float progress = static_cast<float>(current / duration);
        const float progress_x = pos.x + progress * width;
        dl->AddRectFilled(ImVec2(pos.x, pos.y + height - 3), ImVec2(progress_x, pos.y + height),
                          t.primary_u32(), 0.0f);

        // Playhead
        dl->AddTriangleFilled(ImVec2(progress_x - PLAYHEAD_HANDLE_SIZE, pos.y),
                              ImVec2(progress_x + PLAYHEAD_HANDLE_SIZE, pos.y),
                              ImVec2(progress_x, pos.y + PLAYHEAD_HANDLE_SIZE + 2), t.text_u32());
        dl->AddLine(ImVec2(progress_x, pos.y + PLAYHEAD_HANDLE_SIZE),
                    ImVec2(progress_x, pos.y + height), t.text_u32(), 2.0f);

        // Invisible button for scrubbing
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##timeline", ImVec2(width, height));

        if (ImGui::IsItemHovered() || scrubbing_) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                scrubbing_ = true;
                player_->pause();
            }
        }

        if (scrubbing_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float mouse_x = ImGui::GetMousePos().x;
                const float rel_x = std::clamp((mouse_x - pos.x) / width, 0.0f, 1.0f);
                player_->seek(rel_x * duration);
                texture_needs_update_ = true;
            } else {
                scrubbing_ = false;
            }
        }

        ImGui::Dummy(ImVec2(0, height));
    }

    void VideoExtractorDialog::renderTrimControls() {
        if (!player_->isOpen())
            return;

        const auto& t = lfs::vis::theme();
        const float duration = static_cast<float>(player_->duration());

        ImGui::Text("Trim Range:");
        ImGui::SameLine();

        // Start time
        ImGui::PushItemWidth(80);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              lfs::vis::toU32WithAlpha(t.palette.success, 0.2f));
        if (ImGui::DragFloat("##trim_start", &trim_start_, 0.1f, 0.0f, trim_end_ - 0.1f, "%.1fs")) {
            trim_start_ = std::clamp(trim_start_, 0.0f, trim_end_ - 0.1f);
        }
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Set##start")) {
            trim_start_ = static_cast<float>(player_->currentTime());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set start to current position");

        ImGui::SameLine();
        ImGui::Text("-");
        ImGui::SameLine();

        // End time
        ImGui::PushItemWidth(80);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, lfs::vis::toU32WithAlpha(t.palette.error, 0.2f));
        if (ImGui::DragFloat("##trim_end", &trim_end_, 0.1f, trim_start_ + 0.1f, duration, "%.1fs")) {
            trim_end_ = std::clamp(trim_end_, trim_start_ + 0.1f, duration);
        }
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Set##end")) {
            trim_end_ = static_cast<float>(player_->currentTime());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set end to current position");

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            trim_start_ = 0.0f;
            trim_end_ = duration;
        }

        // Show estimated frame count
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(~%d frames)", calculateEstimatedFrames());
    }

    void VideoExtractorDialog::renderFileSelection() {
        ImGui::SeparatorText("Input Video");

        // Video file selection with browse button inline
        ImGui::Text("Video:");
        ImGui::SameLine();

        std::string video_display =
            video_path_.empty() ? "No file selected" : video_path_.filename().string();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", video_display.c_str());

        ImGui::SameLine();
        if (ImGui::Button("Browse...##video")) {
            const auto path = OpenVideoFileDialog();
            if (!path.empty()) {
                openVideo(path);
            }
        }

        // Output directory
        ImGui::Text("Output:");
        ImGui::SameLine();

        std::string output_display =
            output_dir_.empty() ? "No directory selected" : output_dir_.string();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", output_display.c_str());

        ImGui::SameLine();
        if (ImGui::Button("Browse...##output")) {
            const auto path = SelectFolderDialog("Select Output Folder");
            if (!path.empty()) {
                output_dir_ = path;
            }
        }
    }

    void VideoExtractorDialog::renderExtractionSettings() {
        ImGui::SeparatorText("Extraction Settings");

        // Mode selection
        ImGui::Text("Mode:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        std::array<const char*, 2> modes = {"FPS-based", "Frame Interval"};
        ImGui::Combo("##mode", &mode_selection_, modes.data(), static_cast<int>(modes.size()));

        ImGui::SameLine(0, 20);

        // Mode-specific settings
        if (mode_selection_ == 0) {
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("FPS##fps", &fps_, 0.1f, 30.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Extract frames at this rate");
        } else {
            ImGui::SetNextItemWidth(100);
            ImGui::SliderInt("Every##interval", &frame_interval_, 1, 100, "%d frames");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Extract every Nth frame");
        }
    }

    void VideoExtractorDialog::renderFormatSettings() {
        ImGui::SeparatorText("Output Format");

        // Format and quality on same line
        ImGui::Text("Format:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        std::array<const char*, 2> formats = {"PNG (lossless)", "JPEG (smaller)"};
        ImGui::Combo("##format", &format_selection_, formats.data(), static_cast<int>(formats.size()));

        if (format_selection_ == 1) {
            ImGui::SameLine(0, 20);
            ImGui::SetNextItemWidth(100);
            ImGui::SliderInt("Quality##quality", &jpg_quality_, 50, 100, "%d%%");
        }
    }

    void VideoExtractorDialog::renderResolutionSettings() {
        ImGui::SeparatorText("Resolution");

        std::array<const char*, 3> res_modes = {"Original", "Scale", "Custom"};
        ImGui::Text("Resolution:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("##resolution_mode", &resolution_mode_, res_modes.data(),
                     static_cast<int>(res_modes.size()));

        ImGui::SameLine(0, 20);

        if (resolution_mode_ == 1) {
            std::array<const char*, 4> scales = {"25%", "50%", "75%", "100%"};
            ImGui::SetNextItemWidth(80);
            ImGui::Combo("##scale", &scale_selection_, scales.data(), static_cast<int>(scales.size()));
        } else if (resolution_mode_ == 2) {
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("##custom_w", &custom_width_, 0, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Width");
            ImGui::SameLine();
            ImGui::Text("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("##custom_h", &custom_height_, 0, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Height");

            custom_width_ = std::max(16, custom_width_);
            custom_height_ = std::max(16, custom_height_);
        }

        // Show output resolution preview
        if (player_->isOpen()) {
            int out_w = player_->width();
            int out_h = player_->height();

            if (resolution_mode_ == 1) {
                static constexpr std::array<float, 4> scale_values = {0.25f, 0.5f, 0.75f, 1.0f};
                float scale = scale_values[scale_selection_];
                out_w = static_cast<int>(out_w * scale);
                out_h = static_cast<int>(out_h * scale);
            } else if (resolution_mode_ == 2) {
                out_w = custom_width_;
                out_h = custom_height_;
            }

            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Output: %dx%d", out_w, out_h);
        }
    }

    void VideoExtractorDialog::renderOutputSettings() {
        ImGui::SeparatorText("Output Naming");

        ImGui::Text("Pattern:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##pattern", filename_pattern_.data(), filename_pattern_.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use %%d for frame number (e.g., frame_%%04d for zero-padded)");

        ImGui::SameLine();
        const char* ext = format_selection_ == 0 ? ".png" : ".jpg";
        char preview[128];
        std::snprintf(preview, sizeof(preview), filename_pattern_.data(), 1);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Example: %s%s", preview, ext);
    }

    void VideoExtractorDialog::render(bool* p_open) {
        if (!*p_open) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(700, 700), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Extract Frames from Video", p_open)) {
            // Video preview at top
            renderVideoPreview();

            ImGui::Spacing();

            // Transport controls
            renderTransportControls();

            ImGui::Spacing();

            // Timeline
            renderTimeline();

            ImGui::Spacing();

            // Trim controls
            renderTrimControls();

            ImGui::Spacing();

            // File selection
            renderFileSelection();

            ImGui::Spacing();

            // Extraction settings
            renderExtractionSettings();

            ImGui::Spacing();

            // Format settings
            renderFormatSettings();

            ImGui::Spacing();

            // Resolution settings
            renderResolutionSettings();

            ImGui::Spacing();

            // Output naming
            renderOutputSettings();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Start button
            const bool can_start =
                !video_path_.empty() && !output_dir_.empty() && !extracting_.load();

            if (!can_start) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button("Start Extraction", ImVec2(150, 30))) {
                if (on_start_extraction_) {
                    VideoExtractionParams params;
                    params.video_path = video_path_;
                    params.output_dir = output_dir_;
                    params.mode =
                        mode_selection_ == 0 ? io::ExtractionMode::FPS : io::ExtractionMode::INTERVAL;
                    params.fps = static_cast<double>(fps_);
                    params.frame_interval = frame_interval_;
                    params.format = format_selection_ == 0 ? io::ImageFormat::PNG : io::ImageFormat::JPG;
                    params.jpg_quality = jpg_quality_;
                    params.start_time = static_cast<double>(trim_start_);
                    params.end_time = static_cast<double>(trim_end_);

                    // Resolution settings
                    static constexpr std::array<io::ResolutionMode, 3> res_modes = {
                        io::ResolutionMode::Original,
                        io::ResolutionMode::Scale,
                        io::ResolutionMode::Custom};
                    static constexpr std::array<float, 4> scale_values = {0.25f, 0.5f, 0.75f, 1.0f};

                    params.resolution_mode = res_modes[resolution_mode_];
                    params.scale = scale_values[scale_selection_];
                    params.custom_width = custom_width_;
                    params.custom_height = custom_height_;

                    // Filename pattern
                    params.filename_pattern = filename_pattern_.data();

                    extracting_.store(true);
                    current_frame_.store(0);
                    total_frames_.store(0);
                    error_message_.clear();
                    show_completion_message_ = false;

                    on_start_extraction_(params);
                }
            }

            if (!can_start) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(100, 30))) {
                *p_open = false;
            }

            if (video_path_.empty() || output_dir_.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   "Select video and output directory");
            }

            // Show progress
            if (extracting_.load()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                int current = current_frame_.load();
                int total = total_frames_.load();

                if (total > 0) {
                    float progress = static_cast<float>(current) / static_cast<float>(total);
                    ImGui::Text("Extracting: %d / %d frames", current, total);
                    ImGui::ProgressBar(progress, ImVec2(-1, 0));
                } else {
                    ImGui::Text("Starting extraction...");
                    ImGui::ProgressBar(0.0f, ImVec2(-1, 0));
                }
            }

            // Show completion message
            if (show_completion_message_ && !extracting_.load()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                int total = current_frame_.load();
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                   "Extraction completed successfully!");
                ImGui::Text("Extracted %d frames", total);

                if (ImGui::Button("OK", ImVec2(100, 30))) {
                    show_completion_message_ = false;
                    current_frame_.store(0);
                    total_frames_.store(0);
                }
            }

            // Show error if any
            if (!error_message_.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Error: %s", error_message_.c_str());

                if (ImGui::Button("Dismiss", ImVec2(100, 30))) {
                    error_message_.clear();
                }
            }
        }
        ImGui::End();
    }

} // namespace lfs::gui
