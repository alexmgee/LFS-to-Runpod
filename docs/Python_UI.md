# LichtFeld Studio Python UI Framework

Create custom UI panels and widgets using Python with the ImGui-backed UI system.

---

## Panel System

### Defining a Panel

```python
import lichtfeld as lf

class MyPanel:
    # Panel metadata
    panel_label = "My Custom Panel"
    panel_space = "SIDE_PANEL"  # SIDE_PANEL, FLOATING, VIEWPORT_OVERLAY
    panel_order = 100           # Lower = higher in list

    def __init__(self):
        self.value = 0.5
        self.name = ""
        self.enabled = True

    @classmethod
    def poll(cls, ctx):
        """Optional: return False to hide panel conditionally"""
        return ctx.has_scene

    def draw(self, ui):
        """Called every frame to render the panel"""
        ui.heading("Settings")

        changed, self.enabled = ui.checkbox("Enabled", self.enabled)
        changed, self.value = ui.slider_float("Value", self.value, 0.0, 1.0)

        if ui.button("Apply"):
            self.apply_settings()

# Register the panel
lf.ui.register_panel(MyPanel)
```

### Panel Spaces

| Space | Description |
|-------|-------------|
| `SIDE_PANEL` | Collapsible section in the sidebar |
| `FLOATING` | Standalone draggable window |
| `VIEWPORT_OVERLAY` | Transparent overlay on the 3D viewport |

### Panel Management

```python
lf.ui.register_panel(MyPanel)
lf.ui.unregister_panel(MyPanel)
lf.ui.unregister_all_panels()

lf.ui.set_panel_enabled("My Custom Panel", False)
lf.ui.is_panel_enabled("My Custom Panel")
lf.ui.get_panel_names("SIDE_PANEL")  # List panels in a space
```

### Poll Context

The `poll()` classmethod receives a `PanelContext` with:

| Property | Type | Description |
|----------|------|-------------|
| `ctx.is_training` | bool | Training is active |
| `ctx.has_scene` | bool | Scene is loaded |
| `ctx.has_selection` | bool | Gaussians are selected |
| `ctx.num_gaussians` | int | Total gaussian count |
| `ctx.iteration` | int | Current training iteration |
| `ctx.loss` | float | Current loss value |

---

## UI Widgets

All widgets are called on the `ui` parameter passed to `draw()`.

### Text

```python
ui.label("Simple text")
ui.heading("Bold heading")
ui.text_colored("Warning!", (1.0, 0.5, 0.0, 1.0))  # RGBA
ui.bullet_text("Bullet point")
ui.text_selectable("Copyable text", height=100.0)
```

### Buttons

```python
if ui.button("Click Me"):
    do_something()

if ui.button("Sized", (120, 30)):  # width, height
    pass

if ui.small_button("Small"):
    pass

if ui.color_button("##color", (1.0, 0.0, 0.0, 1.0), (20, 20)):
    pass
```

### Checkboxes & Radio Buttons

```python
changed, self.enabled = ui.checkbox("Enable Feature", self.enabled)

changed, self.choice = ui.radio_button("Option A", self.choice, 0)
changed, self.choice = ui.radio_button("Option B", self.choice, 1)
changed, self.choice = ui.radio_button("Option C", self.choice, 2)
```

### Input Fields

```python
changed, self.name = ui.input_text("Name", self.name)
changed, self.scale = ui.input_float("Scale", self.scale)
changed, self.count = ui.input_int("Count", self.count)

# Path input with browse button
changed, self.path = ui.path_input("Output", self.path, folder_mode=True)
changed, self.file = ui.path_input("Image", self.file, folder_mode=False)
```

### Sliders

```python
changed, self.value = ui.slider_float("Float", self.value, 0.0, 1.0)
changed, self.amount = ui.slider_int("Int", self.amount, 0, 100)

# Multi-component sliders
changed, self.vec2 = ui.slider_float2("Vec2", self.vec2, 0.0, 1.0)
changed, self.vec3 = ui.slider_float3("Vec3", self.vec3, -1.0, 1.0)
```

### Drag Inputs

```python
changed, self.value = ui.drag_float("Value", self.value, speed=0.01, min=0.0, max=10.0)
changed, self.index = ui.drag_int("Index", self.index, speed=1, min=0, max=100)
```

### Color Pickers

```python
changed, self.color = ui.color_edit3("Color", self.color)       # (r, g, b)
changed, self.color = ui.color_edit4("Color", self.color)       # (r, g, b, a)
```

### Dropdowns & Lists

```python
options = ["Option A", "Option B", "Option C"]
changed, self.selected = ui.combo("Select", self.selected, options)

changed, self.selected = ui.listbox("Items", self.selected, options, height_items=5)
```

### Progress Bar

```python
ui.progress_bar(0.75)                          # 75%
ui.progress_bar(0.5, "Loading...")             # With overlay text
```

---

## Layout

### Spacing & Separators

```python
ui.separator()          # Horizontal line
ui.spacing()            # Vertical space
ui.new_line()           # Force new line
ui.same_line()          # Next widget on same line
ui.same_line(100)       # With offset
```

### Indentation

```python
ui.indent(20.0)
ui.label("Indented content")
ui.unindent(20.0)
```

### Width Control

```python
ui.set_next_item_width(200)
changed, value = ui.input_float("Fixed Width", value)
```

### Groups

```python
ui.begin_group()
ui.label("Grouped")
ui.button("Together")
ui.end_group()
```

### Collapsible Sections

```python
if ui.collapsing_header("Advanced Settings"):
    ui.label("Hidden by default")

if ui.collapsing_header("Open Section", default_open=True):
    ui.label("Visible by default")
```

### Tree Nodes

```python
if ui.tree_node("Parent"):
    ui.label("Child content")
    if ui.tree_node("Nested"):
        ui.label("Deeply nested")
        ui.tree_pop()
    ui.tree_pop()
```

### Tables

```python
if ui.begin_table("my_table", 3):
    ui.table_next_row()
    ui.table_next_column()
    ui.label("Col 1")
    ui.table_next_column()
    ui.label("Col 2")
    ui.table_next_column()
    ui.label("Col 3")

    ui.table_next_row()
    ui.table_set_column_index(0)  # Jump to column
    ui.label("Row 2")

    ui.end_table()
```

---

## Interaction

### Tooltips

```python
ui.button("Hover Me")
ui.set_tooltip("This tooltip appears on hover")

# Or check manually
if ui.is_item_hovered():
    ui.set_tooltip("Custom tooltip")
```

### Click Detection

```python
ui.button("Click Me")
if ui.is_item_clicked(0):  # 0 = left, 1 = right, 2 = middle
    print("Clicked!")
```

### ID Management

Prevent widget ID conflicts:

```python
for i, item in enumerate(items):
    ui.push_id(i)  # or ui.push_id(f"item_{i}")
    if ui.button("Delete"):
        delete_item(i)
    ui.pop_id()
```

---

## UI Hooks

Inject custom UI into existing panels:

```python
# Decorator style
@lf.ui.hook("training", "status")
def add_custom_info(ui):
    ui.separator()
    ui.label("Custom training info")

@lf.ui.hook("training", "controls", position="prepend")
def add_before(ui):
    ui.label("Prepended content")

# Functional style
def my_hook(ui):
    ui.label("Hook content")

lf.ui.add_hook("panel_name", "section_name", my_hook, position="append")
lf.ui.remove_hook("panel_name", "section_name", my_hook)
lf.ui.clear_hooks("panel_name", "section_name")
lf.ui.clear_hooks("panel_name")  # Clear all sections
lf.ui.clear_all_hooks()

# List registered hook points
points = lf.ui.get_hook_points()
```

---

## File Dialogs

```python
# Folder selection
folder = lf.ui.open_folder_dialog("Select Output Folder", "/default/path")
if folder:
    print(f"Selected: {folder}")

# Image file selection
image = lf.ui.open_image_dialog("/images")
if image:
    print(f"Selected: {image}")
```

---

## Theme Access

Access the current UI theme colors and sizes:

```python
theme = lf.ui.theme()

# Palette (all are RGBA tuples)
theme.palette.background
theme.palette.surface
theme.palette.primary
theme.palette.secondary
theme.palette.text
theme.palette.text_dim
theme.palette.border
theme.palette.success
theme.palette.warning
theme.palette.error
theme.palette.info

# Sizes
theme.sizes.window_rounding
theme.sizes.frame_rounding
theme.sizes.border_size
theme.sizes.window_padding   # (x, y)
theme.sizes.frame_padding    # (x, y)
theme.sizes.item_spacing     # (x, y)
```

---

## Tool Switching

```python
lf.ui.set_tool("selection")
lf.ui.set_tool("translate")
lf.ui.set_tool("rotate")
lf.ui.set_tool("scale")
lf.ui.set_tool("brush")
lf.ui.set_tool("cropbox")
lf.ui.set_tool("none")
```

---

## Property Widgets

Auto-generate widgets from property metadata:

```python
def draw(self, ui):
    # Automatically creates appropriate widget based on property type
    changed, value = ui.prop(params, "learning_rate")
    changed, value = ui.prop(params, "iterations", text="Max Iterations")
```

---

## Complete Example

```python
import lichtfeld as lf

class GaussianFilterPanel:
    panel_label = "Gaussian Filter"
    panel_space = "SIDE_PANEL"
    panel_order = 50

    def __init__(self):
        self.opacity_threshold = 0.01
        self.scale_threshold = 0.001
        self.preview = True
        self.selected_only = False

    @classmethod
    def poll(cls, ctx):
        return ctx.has_scene and ctx.num_gaussians > 0

    def draw(self, ui):
        ui.heading("Filter Settings")

        changed, self.preview = ui.checkbox("Preview", self.preview)
        changed, self.selected_only = ui.checkbox("Selected Only", self.selected_only)

        ui.separator()

        ui.label("Thresholds")
        changed, self.opacity_threshold = ui.slider_float(
            "Min Opacity", self.opacity_threshold, 0.0, 1.0
        )
        changed, self.scale_threshold = ui.drag_float(
            "Min Scale", self.scale_threshold, 0.0001, 0.0, 0.1
        )

        ui.separator()

        if ui.collapsing_header("Statistics"):
            scene = lf.get_scene()
            if scene:
                model = scene.combined_model()
                if model:
                    ui.label(f"Total: {model.num_points:,}")
                    ui.label(f"Visible: {model.visible_count():,}")

        ui.separator()

        ui.begin_group()
        if ui.button("Apply Filter", (100, 0)):
            self.apply_filter()
        ui.same_line()
        if ui.button("Reset", (60, 0)):
            self.reset()
        ui.end_group()

        ui.set_tooltip("Apply filter to remove low-quality gaussians")

    def apply_filter(self):
        scene = lf.get_scene()
        if not scene:
            return

        model = scene.training_model()
        if not model:
            return

        opacity = model.get_opacity().squeeze()
        mask = opacity < self.opacity_threshold
        model.soft_delete(mask)
        lf.log.info(f"Filtered {mask.sum_scalar():.0f} gaussians")

    def reset(self):
        self.opacity_threshold = 0.01
        self.scale_threshold = 0.001

lf.ui.register_panel(GaussianFilterPanel)
```

---

## Widget Reference

| Category | Widgets |
|----------|---------|
| **Text** | `label`, `heading`, `text_colored`, `bullet_text`, `text_selectable` |
| **Buttons** | `button`, `small_button`, `color_button` |
| **Toggle** | `checkbox`, `radio_button` |
| **Input** | `input_text`, `input_float`, `input_int`, `path_input` |
| **Sliders** | `slider_float`, `slider_int`, `slider_float2`, `slider_float3` |
| **Drags** | `drag_float`, `drag_int` |
| **Color** | `color_edit3`, `color_edit4` |
| **Selection** | `combo`, `listbox` |
| **Layout** | `separator`, `spacing`, `same_line`, `new_line`, `indent`, `unindent`, `set_next_item_width` |
| **Grouping** | `begin_group`, `end_group`, `collapsing_header`, `tree_node`, `tree_pop` |
| **Tables** | `begin_table`, `end_table`, `table_next_row`, `table_next_column`, `table_set_column_index` |
| **Misc** | `progress_bar`, `set_tooltip`, `is_item_hovered`, `is_item_clicked`, `push_id`, `pop_id` |
