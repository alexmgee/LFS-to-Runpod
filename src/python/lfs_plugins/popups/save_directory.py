# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Save directory popup for dataset import configuration."""

from typing import Optional, Callable
from dataclasses import dataclass
from pathlib import Path


@dataclass
class DatasetLoadParams:
    """Parameters for loading a dataset."""

    dataset_path: Path
    output_path: Path
    init_path: Optional[Path] = None


class SaveDirectoryPopup:
    """Popup for configuring dataset import paths."""

    POPUP_WIDTH = 560
    POPUP_HEIGHT = 360
    INPUT_WIDTH = 380
    BUTTON_WIDTH = 100
    BUTTON_SPACING = 8

    def __init__(self):
        self._open = False
        self._pending_open = False
        self._dataset_info = None
        self._output_path = ""
        self._init_path = ""
        self._use_custom_init = False
        self._on_confirm: Optional[Callable[[DatasetLoadParams], None]] = None

    @property
    def is_open(self) -> bool:
        return self._open

    def show(self, dataset_path: str, on_confirm: Optional[Callable[[DatasetLoadParams], None]] = None):
        """Show the popup with the given dataset path."""
        import lichtfeld as lf

        self._dataset_info = lf.detect_dataset_info(dataset_path)
        self._output_path = str(Path(self._dataset_info.base_path) / "output")
        self._init_path = ""
        self._use_custom_init = False
        self._on_confirm = on_confirm
        self._pending_open = True

    def draw(self, layout):
        """Draw the popup. Called every frame."""
        import lichtfeld as lf
        tr = lf.ui.tr

        if self._pending_open:
            layout.set_next_window_pos_center()
            layout.set_next_window_size((self.POPUP_WIDTH, self.POPUP_HEIGHT))
            layout.open_popup(tr("load_dataset_popup.title"))
            self._pending_open = False
            self._open = True

        if not self._open:
            return

        if layout.begin_popup_modal(tr("load_dataset_popup.title")):
            info = self._dataset_info

            # Header
            layout.text_colored("Dataset", (0.3, 0.7, 1.0, 1.0))
            layout.same_line()
            layout.text_colored("|", (0.5, 0.5, 0.5, 1.0))
            layout.same_line()
            layout.label(tr("load_dataset_popup.configure_paths"))

            layout.spacing()
            layout.separator()
            layout.spacing()

            # Dataset info
            layout.text_colored(tr("load_dataset_popup.images_dir"), (0.6, 0.6, 0.6, 1.0))
            layout.same_line()
            layout.label(str(info.images_path))
            layout.same_line()
            layout.text_colored(f"({info.image_count} images)", (0.6, 0.6, 0.6, 1.0))

            layout.text_colored(tr("load_dataset_popup.sparse_dir"), (0.6, 0.6, 0.6, 1.0))
            layout.same_line()
            layout.label(str(info.sparse_path))

            if info.has_masks:
                layout.text_colored(tr("load_dataset_popup.masks_dir"), (0.6, 0.6, 0.6, 1.0))
                layout.same_line()
                layout.label(str(info.masks_path))
                layout.same_line()
                layout.text_colored(f"({info.mask_count} masks)", (0.6, 0.6, 0.6, 1.0))

            layout.spacing()

            # Custom init file option
            _, self._use_custom_init = layout.checkbox("Custom init file (replaces points3D)", self._use_custom_init)
            if self._use_custom_init:
                layout.set_next_item_width(self.INPUT_WIDTH)
                _, self._init_path = layout.input_text("##init_path", self._init_path)
                layout.same_line()
                if layout.button(tr("common.browse") + "##init"):
                    path = lf.ui.open_ply_file_dialog(str(info.base_path))
                    if path:
                        self._init_path = path

            layout.spacing()
            layout.separator()
            layout.spacing()

            # Output path
            layout.text_colored(tr("load_dataset_popup.output_dir"), (0.6, 0.6, 0.6, 1.0))
            layout.set_next_item_width(self.INPUT_WIDTH)
            _, self._output_path = layout.input_text("##output_path", self._output_path)
            layout.same_line()
            if layout.button(tr("common.browse") + "##output"):
                path = lf.ui.open_dataset_folder_dialog()
                if path:
                    self._output_path = path

            layout.spacing()
            layout.text_wrapped(tr("load_dataset_popup.help_text"))
            layout.spacing()
            layout.separator()
            layout.spacing()

            avail_width = layout.get_content_region_avail()[0]
            total_width = self.BUTTON_WIDTH * 2 + self.BUTTON_SPACING
            layout.set_cursor_pos_x(layout.get_cursor_pos()[0] + avail_width - total_width)

            if layout.button_styled(tr("common.cancel"), "secondary", (self.BUTTON_WIDTH, 0)) or lf.ui.is_key_pressed(lf.ui.Key.ESCAPE):
                self._open = False
                layout.close_current_popup()

            layout.same_line()

            if layout.button_styled(tr("common.load"), "success", (self.BUTTON_WIDTH, 0)) or lf.ui.is_key_pressed(lf.ui.Key.ENTER):
                self._open = False
                layout.close_current_popup()
                if self._on_confirm:
                    params = DatasetLoadParams(
                        dataset_path=Path(info.base_path),
                        output_path=Path(self._output_path),
                        init_path=Path(self._init_path) if self._use_custom_init and self._init_path else None,
                    )
                    self._on_confirm(params)

            layout.end_popup_modal()
        else:
            self._open = False
