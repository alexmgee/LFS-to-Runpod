# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exit confirmation popup dialog."""

from typing import Optional, Callable


class ExitConfirmationPopup:
    """Popup dialog for confirming application exit."""

    POPUP_WIDTH = 340
    POPUP_HEIGHT = 150
    BUTTON_WIDTH = 100
    BUTTON_SPACING = 12

    def __init__(self):
        self._open = False
        self._pending_open = False
        self._on_confirm: Optional[Callable[[], None]] = None
        self._on_cancel: Optional[Callable[[], None]] = None

    @property
    def is_open(self) -> bool:
        return self._open

    def show(
        self,
        on_confirm: Optional[Callable[[], None]] = None,
        on_cancel: Optional[Callable[[], None]] = None,
    ):
        """Show the exit confirmation popup."""
        import lichtfeld as lf

        self._on_confirm = on_confirm
        self._on_cancel = on_cancel
        self._pending_open = True
        lf.ui.set_exit_popup_open(True)

    def draw(self, layout):
        """Draw the popup. Called every frame."""
        import lichtfeld as lf
        tr = lf.ui.tr

        if self._pending_open:
            layout.set_next_window_pos_viewport_center()
            layout.set_next_window_size((self.POPUP_WIDTH, self.POPUP_HEIGHT))
            layout.open_popup(tr("exit_popup.title"))
            self._pending_open = False
            self._open = True

        if not self._open:
            return

        layout.set_next_window_focus()
        layout.push_modal_style()

        if layout.begin_popup_modal(tr("exit_popup.title")):
            layout.label_centered(tr("exit_popup.message"))
            layout.spacing()
            layout.text_colored_centered(tr("exit_popup.unsaved_warning"), (0.6, 0.6, 0.6, 1.0))
            layout.spacing()
            layout.spacing()

            avail_width = layout.get_content_region_avail()[0]
            total_width = self.BUTTON_WIDTH * 2 + self.BUTTON_SPACING
            layout.set_cursor_pos_x(layout.get_cursor_pos()[0] + (avail_width - total_width) / 2)

            if layout.button_styled(tr("common.cancel"), "secondary", (self.BUTTON_WIDTH, 0)):
                self._close(layout)
                if self._on_cancel:
                    self._on_cancel()

            layout.same_line(0, self.BUTTON_SPACING)

            if layout.button_styled(tr("exit_popup.exit"), "error", (self.BUTTON_WIDTH, 0)):
                self._close(layout)
                if self._on_confirm:
                    self._on_confirm()

            if lf.ui.is_key_pressed(lf.ui.Key.ESCAPE):
                self._close(layout)
                if self._on_cancel:
                    self._on_cancel()

            layout.end_popup_modal()
        else:
            self._open = False
            lf.ui.set_exit_popup_open(False)

        layout.pop_modal_style()

    def _close(self, layout):
        import lichtfeld as lf
        self._open = False
        lf.ui.set_exit_popup_open(False)
        layout.close_current_popup()
