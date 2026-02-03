# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sequencer panel - camera animation settings and video export."""

from enum import IntEnum

import lichtfeld as lf


class VideoPreset(IntEnum):
    YOUTUBE_1080P = 0
    YOUTUBE_4K = 1
    HD_720P = 2
    TIKTOK = 3
    TIKTOK_HD = 4
    INSTAGRAM_SQUARE = 5
    INSTAGRAM_PORTRAIT = 6
    CUSTOM = 7


PRESET_INFO = {
    VideoPreset.YOUTUBE_1080P: {"name": "YouTube 1080p", "width": 1920, "height": 1080, "fps": 30, "crf": 18, "desc": "1920x1080 @ 30fps"},
    VideoPreset.YOUTUBE_4K: {"name": "YouTube 4K", "width": 3840, "height": 2160, "fps": 30, "crf": 18, "desc": "3840x2160 @ 30fps"},
    VideoPreset.HD_720P: {"name": "720p HD", "width": 1280, "height": 720, "fps": 30, "crf": 20, "desc": "1280x720 @ 30fps"},
    VideoPreset.TIKTOK: {"name": "TikTok", "width": 1080, "height": 1920, "fps": 30, "crf": 18, "desc": "1080x1920 @ 30fps (9:16)"},
    VideoPreset.TIKTOK_HD: {"name": "TikTok HD", "width": 1080, "height": 1920, "fps": 60, "crf": 18, "desc": "1080x1920 @ 60fps (9:16)"},
    VideoPreset.INSTAGRAM_SQUARE: {"name": "Instagram Square", "width": 1080, "height": 1080, "fps": 30, "crf": 18, "desc": "1080x1080 @ 30fps (1:1)"},
    VideoPreset.INSTAGRAM_PORTRAIT: {"name": "Instagram Portrait", "width": 1080, "height": 1350, "fps": 30, "crf": 18, "desc": "1080x1350 @ 30fps (4:5)"},
    VideoPreset.CUSTOM: {"name": "Custom", "width": 1920, "height": 1080, "fps": 30, "crf": 18, "desc": "Custom resolution"},
}

SPEED_VALUES = [0.25, 0.5, 1.0, 2.0, 4.0]
SPEED_LABELS = ["0.25x", "0.5x", "1x", "2x", "4x"]
SNAP_VALUES = [0.25, 0.5, 1.0, 2.0]
SNAP_LABELS = ["0.25s", "0.5s", "1s", "2s"]
FPS_VALUES = [24, 30, 60]
FPS_LABELS = ["24 fps", "30 fps", "60 fps"]

MIN_WIDTH, MAX_WIDTH = 320, 7680
MIN_HEIGHT, MAX_HEIGHT = 240, 4320


class _VideoExportState:
    """Local state for video export settings."""
    preset = 0  # YOUTUBE_1080P
    custom_width = 1920
    custom_height = 1080
    framerate = 30
    quality = 18


_video_state = _VideoExportState()


def draw_sequencer_section(layout):
    """Draw the sequencer section UI."""
    state = lf.ui.get_sequencer_state()
    if not state:
        return

    has_keyframes = lf.ui.has_keyframes()

    lf.ui.section_header("SEQUENCER")

    _, state.show_camera_path = layout.checkbox("Show Camera Path", state.show_camera_path)
    if layout.is_item_hovered():
        layout.tooltip("Display camera path in viewport")

    speed_idx = 2
    for i, val in enumerate(SPEED_VALUES):
        if abs(state.playback_speed - val) < 0.01:
            speed_idx = i
            break
    changed, speed_idx = layout.combo("Speed", speed_idx, SPEED_LABELS)
    if changed:
        state.playback_speed = SPEED_VALUES[speed_idx]
        lf.ui.set_playback_speed(state.playback_speed)

    _, state.snap_to_grid = layout.checkbox("Snap to Grid", state.snap_to_grid)
    if state.snap_to_grid:
        layout.same_line()
        layout.push_item_width(60)
        snap_idx = 1
        for i, val in enumerate(SNAP_VALUES):
            if abs(state.snap_interval - val) < 0.01:
                snap_idx = i
                break
        changed, snap_idx = layout.combo("##snap_interval", snap_idx, SNAP_LABELS)
        if changed:
            state.snap_interval = SNAP_VALUES[snap_idx]
        layout.pop_item_width()

    _, state.follow_playback = layout.checkbox("Follow Playback", state.follow_playback)
    if layout.is_item_hovered():
        layout.tooltip("Camera follows playhead during playback")

    layout.set_next_item_width(-1)
    _, state.pip_preview_scale = layout.slider_float("Preview Size", state.pip_preview_scale, 0.5, 2.0)
    if layout.is_item_hovered():
        layout.tooltip("Scale the preview window")

    layout.spacing()

    avail_w, _ = layout.get_content_region_avail()
    btn_width = (avail_w - 8) * 0.5  # 8 is approximate item spacing

    if not has_keyframes:
        layout.begin_disabled()
    if layout.button("Save Path...", (btn_width, 0)):
        path = lf.ui.save_json_file_dialog("camera_path")
        if path:
            if lf.ui.save_camera_path(path):
                lf.log_info(f"Camera path saved to {path}")
            else:
                lf.log_error(f"Failed to save camera path to {path}")
    if not has_keyframes:
        layout.end_disabled()

    layout.same_line()

    if layout.button("Load Path...", (btn_width, 0)):
        path = lf.ui.open_json_file_dialog()
        if path:
            if lf.ui.load_camera_path(path):
                lf.log_info(f"Camera path loaded from {path}")
            else:
                lf.log_error(f"Failed to load camera path from {path}")

    if not has_keyframes:
        layout.begin_disabled()
    if layout.button_styled("Clear All Keyframes", "error"):
        layout.open_popup("Clear Camera Path")
    if not has_keyframes:
        layout.end_disabled()

    if layout.begin_popup_modal("Clear Camera Path"):
        layout.label("Delete all keyframes? This cannot be undone.")
        layout.spacing()
        layout.spacing()

        if layout.button_styled("Cancel", "secondary", (80, 0)) or lf.ui.is_key_pressed(lf.ui.Key.ESCAPE):
            layout.close_current_popup()
        layout.same_line()
        if layout.button_styled("Delete", "error", (80, 0)):
            lf.ui.clear_keyframes()
            layout.close_current_popup()
        layout.end_popup()

    layout.spacing()
    layout.separator()
    layout.spacing()

    _draw_video_export_section(layout, has_keyframes)


def _draw_video_export_section(layout, has_keyframes: bool):
    """Draw the video export settings section."""
    layout.label("Video Export")
    layout.spacing()

    vs = _video_state
    preset = VideoPreset(vs.preset) if 0 <= vs.preset <= 7 else VideoPreset.YOUTUBE_1080P
    current_info = PRESET_INFO[preset]

    preset_names = [info["name"] for info in PRESET_INFO.values()]
    changed, new_preset_idx = layout.combo("Format", int(preset), preset_names)
    if changed:
        new_preset = VideoPreset(new_preset_idx)
        vs.preset = int(new_preset)
        if new_preset != VideoPreset.CUSTOM:
            info = PRESET_INFO[new_preset]
            vs.framerate = info["fps"]
            vs.quality = info["crf"]
        preset = new_preset
        current_info = PRESET_INFO[preset]

    if preset == VideoPreset.CUSTOM:
        _, vs.custom_width = layout.input_int("Width", vs.custom_width, 16, 64)
        _, vs.custom_height = layout.input_int("Height", vs.custom_height, 16, 64)
        vs.custom_width = max(MIN_WIDTH, min(MAX_WIDTH, vs.custom_width))
        vs.custom_height = max(MIN_HEIGHT, min(MAX_HEIGHT, vs.custom_height))

        fps_idx = 1
        if vs.framerate == 24:
            fps_idx = 0
        elif vs.framerate == 60:
            fps_idx = 2
        changed, fps_idx = layout.combo("Framerate", fps_idx, FPS_LABELS)
        if changed:
            vs.framerate = FPS_VALUES[fps_idx]
    else:
        layout.text_disabled(current_info["desc"])

    _, vs.quality = layout.slider_int("Quality", vs.quality, 15, 28)
    if layout.is_item_hovered():
        layout.tooltip("Lower = higher quality, larger file")

    layout.spacing()

    if not has_keyframes:
        layout.begin_disabled()

    if layout.button_styled("Export Video...", "primary"):
        info = PRESET_INFO[preset]
        width = vs.custom_width if preset == VideoPreset.CUSTOM else info["width"]
        height = vs.custom_height if preset == VideoPreset.CUSTOM else info["height"]
        lf.ui.export_video(width, height, vs.framerate, vs.quality)

    if not has_keyframes:
        layout.end_disabled()
