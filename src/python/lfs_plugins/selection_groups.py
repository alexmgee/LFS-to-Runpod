# Selection Groups Panel - Python hook for rendering panel
import lichtfeld as lf

LOCK_ICON_SIZE = 14.0
LOCKED_TINT = (1.0, 0.7, 0.3, 1.0)
UNLOCKED_TINT = (0.6, 0.6, 0.6, 0.6)
ACTIVE_BG_ALPHA = 0.6
ACTIVE_HOVER_ALPHA = 0.7


def _tr(key):
    result = lf.ui.tr(key)
    return result if result else key


class SelectionGroupsPanel:
    def _get_icon(self, name: str) -> int:
        from . import icon_manager
        return icon_manager.get_scene_icon(name)

    def draw(self, layout):
        if lf.ui.get_active_tool() != "builtin.select":
            return

        scene = lf.get_scene()
        if scene is None:
            return
        theme = lf.ui.theme()

        if not layout.collapsing_header(_tr("main_panel.selection_groups"), default_open=True):
            return

        avail_w, _ = layout.get_content_region_avail()
        if layout.button(_tr("main_panel.add_group"), (avail_w, 0)):
            scene.add_selection_group("", (0.0, 0.0, 0.0))

        groups = scene.selection_groups()
        if not groups:
            layout.text_colored(_tr("main_panel.no_selection_groups"), theme.palette.text_dim)
            return

        scene.update_selection_group_counts()
        active_id = scene.active_selection_group
        icon_size = LOCK_ICON_SIZE * layout.get_dpi_scale()
        surface = theme.palette.surface_bright

        for group in groups:
            layout.push_id_int(group.id)
            is_active = group.id == active_id
            is_locked = group.locked
            r, g, b = group.color

            if is_active:
                layout.push_style_color("Header", (r * 0.4, g * 0.4, b * 0.4, ACTIVE_BG_ALPHA))
                layout.push_style_color("HeaderHovered", (r * 0.5, g * 0.5, b * 0.5, ACTIVE_HOVER_ALPHA))

            layout.push_style_color("Button", (0, 0, 0, 0))
            layout.push_style_color("ButtonHovered", (surface[0], surface[1], surface[2], 0.5))
            layout.push_style_color("ButtonActive", (surface[0], surface[1], surface[2], 0.7))

            lock_tex = self._get_icon("locked") if is_locked else self._get_icon("unlocked")
            if lock_tex:
                if layout.image_button("##lock", lock_tex, (icon_size, icon_size),
                                       LOCKED_TINT if is_locked else UNLOCKED_TINT):
                    scene.set_selection_group_locked(group.id, not is_locked)
            elif layout.small_button("L" if is_locked else "U"):
                scene.set_selection_group_locked(group.id, not is_locked)

            layout.pop_style_color(3)

            if layout.is_item_hovered():
                layout.set_tooltip(_tr("tooltip.locked") if is_locked else _tr("tooltip.unlocked"))

            layout.same_line()

            changed, color = layout.color_edit3("##color", list(group.color))
            if changed:
                scene.set_selection_group_color(group.id, tuple(color))

            layout.same_line()

            label = f"> {group.name} ({group.count})" if is_active else f"  {group.name} ({group.count})"
            if layout.selectable(label, is_active):
                scene.active_selection_group = group.id

            if is_active:
                layout.pop_style_color(2)

            if layout.begin_popup_context_item("##ctx"):
                if layout.menu_item(_tr("selection_group.unlock") if is_locked else _tr("selection_group.lock")):
                    scene.set_selection_group_locked(group.id, not is_locked)
                if layout.menu_item(_tr("main_panel.clear")):
                    scene.clear_selection_group(group.id)
                if layout.menu_item(_tr("common.delete")):
                    scene.remove_selection_group(group.id)
                layout.end_popup()

            layout.pop_id()


_panel_instance = None


def _draw_hook(layout):
    global _panel_instance
    if _panel_instance is None:
        _panel_instance = SelectionGroupsPanel()
    _panel_instance.draw(layout)


def register():
    lf.ui.add_hook("rendering", "selection_groups", _draw_hook, "prepend")


def unregister():
    global _panel_instance
    lf.ui.remove_hook("rendering", "selection_groups", _draw_hook)
    _panel_instance = None
