"""Export progress overlay - modal dialog during PLY/SOG export."""

import lichtfeld as lf
from ..types import Panel

BACKDROP_FLAGS = (
    lf.ui.UILayout.WindowFlags.NoTitleBar
    | lf.ui.UILayout.WindowFlags.NoResize
    | lf.ui.UILayout.WindowFlags.NoMove
    | lf.ui.UILayout.WindowFlags.NoScrollbar
    | lf.ui.UILayout.WindowFlags.NoInputs
    | lf.ui.UILayout.WindowFlags.NoBackground
    | lf.ui.UILayout.WindowFlags.NoFocusOnAppearing
    | lf.ui.UILayout.WindowFlags.NoBringToFrontOnFocus
)

DIALOG_FLAGS = (
    lf.ui.UILayout.WindowFlags.NoTitleBar
    | lf.ui.UILayout.WindowFlags.NoResize
    | lf.ui.UILayout.WindowFlags.NoMove
    | lf.ui.UILayout.WindowFlags.NoScrollbar
    | lf.ui.UILayout.WindowFlags.NoCollapse
    | lf.ui.UILayout.WindowFlags.AlwaysAutoResize
)


class ExportProgressOverlay(Panel):
    """Modal overlay showing export progress."""

    label = "##ExportProgress"
    space = "VIEWPORT_OVERLAY"
    order = 150

    @classmethod
    def poll(cls, context):
        if not hasattr(lf.ui, "get_export_state"):
            return False
        state = lf.ui.get_export_state()
        return state.get("active", False)

    def draw(self, layout):
        state = lf.ui.get_export_state()
        if not state.get("active", False):
            return

        vp_x, vp_y = layout.get_viewport_pos()
        vp_w, vp_h = layout.get_viewport_size()
        scale = layout.get_dpi_scale()

        # Backdrop window for dimming
        layout.set_next_window_pos((vp_x, vp_y))
        layout.set_next_window_size((vp_w, vp_h))
        if layout.begin_window("##ExportBackdrop", BACKDROP_FLAGS):
            layout.draw_window_rect_filled(vp_x, vp_y, vp_x + vp_w, vp_y + vp_h, (0.0, 0.0, 0.0, 0.4))
        layout.end_window()

        # Dialog window
        overlay_width = 350.0 * scale
        button_width = 100.0 * scale
        button_height = 30.0 * scale

        overlay_x = vp_x + (vp_w - overlay_width) * 0.5
        overlay_y = vp_y + vp_h * 0.4

        layout.set_next_window_pos((overlay_x, overlay_y))
        layout.set_next_window_size((overlay_width, 0))

        layout.push_style_color("WindowBg", (0.1, 0.1, 0.1, 0.95))
        layout.push_style_var("WindowRounding", 8.0 * scale)
        layout.push_style_var_vec2("WindowPadding", (20 * scale, 15 * scale))

        if layout.begin_window("##ExportProgressWin", DIALOG_FLAGS):
            fmt = state.get("format", "file")
            title = lf.ui.tr("progress.exporting").replace("%s", fmt)
            layout.label(title)
            layout.spacing()

            progress = state.get("progress", 0.0)
            layout.progress_bar(progress, "", -1)

            layout.label(f"{progress * 100:.0f}%")
            layout.same_line()
            layout.label(state.get("stage", ""))

            layout.spacing()

            cursor_x = (overlay_width - button_width) * 0.5 - 20 * scale
            layout.set_cursor_pos_x(cursor_x)
            if layout.button(lf.ui.tr("common.cancel"), (button_width, button_height)):
                lf.ui.cancel_export()

        layout.end_window()
        layout.pop_style_var(2)
        layout.pop_style_color(1)
